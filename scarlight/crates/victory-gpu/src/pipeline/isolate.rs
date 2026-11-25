//! Create host pipelines in a child process so a vkd3d-proton/driver
//! fastfail or abort doesn't kill the game. The child creates the PSO,
//! stores it in a pipeline library blob, and exits; parent loads it.

#![cfg(windows)]
#![allow(unsafe_op_in_unsafe_fn)]

use std::ffi::CString;
use std::path::{Path, PathBuf};

use windows::Win32::Graphics::Direct3D::D3D_FEATURE_LEVEL_11_0;
use windows::Win32::Graphics::Direct3D12::*;
use windows::Win32::Graphics::Dxgi::Common::*;
use windows::core::{HSTRING, Interface, PCWSTR};

/// Serializable pipeline creation request.
#[derive(Clone)]
pub(crate) enum Recipe {
    Mesh {
        root: Vec<u8>,
        ms: Vec<u8>,
        ps: Vec<u8>,
        state: GfxState,
    },
    Graphics {
        root: Vec<u8>,
        vs: Vec<u8>,
        ps: Vec<u8>,
        gs: Option<Vec<u8>>,
        state: GfxState,
        inputs: Vec<InputElem>,
    },
    Compute {
        root: Vec<u8>,
        cs: Vec<u8>,
    },
}

#[derive(Clone)]
pub(crate) struct GfxState {
    pub blend: D3D12_BLEND_DESC,
    pub sample_mask: u32,
    pub rasterizer: D3D12_RASTERIZER_DESC,
    pub depth: D3D12_DEPTH_STENCIL_DESC,
    pub strip_cut: D3D12_INDEX_BUFFER_STRIP_CUT_VALUE,
    pub topology: D3D12_PRIMITIVE_TOPOLOGY_TYPE,
    pub targets: D3D12_RT_FORMAT_ARRAY,
    pub depth_format: DXGI_FORMAT,
    pub samples: DXGI_SAMPLE_DESC,
}

#[derive(Clone)]
pub(crate) struct InputElem {
    pub semantic_name: CString,
    pub semantic_index: u32,
    pub format: DXGI_FORMAT,
    pub input_slot: u32,
    pub aligned_byte_offset: u32,
    pub input_slot_class: D3D12_INPUT_CLASSIFICATION,
    pub instance_data_step_rate: u32,
}

fn write_bytes(w: &mut Vec<u8>, b: &[u8]) {
    w.extend_from_slice(&(b.len() as u32).to_le_bytes());
    w.extend_from_slice(b);
}
fn write_u32(w: &mut Vec<u8>, v: u32) { w.extend_from_slice(&v.to_le_bytes()); }
fn write_u8(w: &mut Vec<u8>, v: u8) { w.push(v); }
fn read_bytes(r: &mut &[u8]) -> Option<Vec<u8>> {
    if r.len() < 4 { return None; }
    let n = u32::from_le_bytes(r[..4].try_into().ok()?) as usize;
    *r = &r[4..];
    if r.len() < n { return None; }
    let b = r[..n].to_vec();
    *r = &r[n..];
    Some(b)
}
fn read_u32(r: &mut &[u8]) -> Option<u32> {
    if r.len() < 4 { return None; }
    let v = u32::from_le_bytes(r[..4].try_into().ok()?);
    *r = &r[4..];
    Some(v)
}
fn read_u8(r: &mut &[u8]) -> Option<u8> {
    if r.is_empty() { return None; }
    let v = r[0]; *r = &r[1..]; Some(v)
}

unsafe fn write_pod<T: Copy>(w: &mut Vec<u8>, v: &T) {
    let b = unsafe { std::slice::from_raw_parts((v as *const T).cast::<u8>(), std::mem::size_of::<T>()) };
    w.extend_from_slice(b);
}
unsafe fn read_pod<T: Copy>(r: &mut &[u8]) -> Option<T> {
    let n = std::mem::size_of::<T>();
    if r.len() < n { return None; }
    let mut v = std::mem::MaybeUninit::<T>::uninit();
    unsafe { std::ptr::copy_nonoverlapping(r.as_ptr(), v.as_mut_ptr().cast::<u8>(), n); }
    *r = &r[n..];
    Some(unsafe { v.assume_init() })
}

impl GfxState {
    fn encode(&self, w: &mut Vec<u8>) {
        unsafe {
            write_pod(w, &self.blend);
            write_u32(w, self.sample_mask);
            write_pod(w, &self.rasterizer);
            write_pod(w, &self.depth);
            write_pod(w, &self.strip_cut);
            write_pod(w, &self.topology);
            write_pod(w, &self.targets);
            write_pod(w, &self.depth_format);
            write_pod(w, &self.samples);
        }
    }
    fn decode(r: &mut &[u8]) -> Option<Self> {
        unsafe {
            Some(GfxState {
                blend: read_pod(r)?,
                sample_mask: read_u32(r)?,
                rasterizer: read_pod(r)?,
                depth: read_pod(r)?,
                strip_cut: read_pod(r)?,
                topology: read_pod(r)?,
                targets: read_pod(r)?,
                depth_format: read_pod(r)?,
                samples: read_pod(r)?,
            })
        }
    }
}

impl Recipe {
    pub(crate) fn encode(&self) -> Vec<u8> {
        let mut w = Vec::new();
        match self {
            Recipe::Mesh { root, ms, ps, state } => {
                write_u8(&mut w, 1);
                write_bytes(&mut w, root);
                write_bytes(&mut w, ms);
                write_bytes(&mut w, ps);
                state.encode(&mut w);
            }
            Recipe::Graphics { root, vs, ps, gs, state, inputs } => {
                write_u8(&mut w, 2);
                write_bytes(&mut w, root);
                write_bytes(&mut w, vs);
                write_bytes(&mut w, ps);
                match gs { Some(g) => { write_u8(&mut w, 1); write_bytes(&mut w, g); } None => write_u8(&mut w, 0) }
                state.encode(&mut w);
                write_u32(&mut w, inputs.len() as u32);
                for e in inputs {
                    write_bytes(&mut w, e.semantic_name.as_bytes_with_nul());
                    write_u32(&mut w, e.semantic_index);
                    unsafe { write_pod(&mut w, &e.format); }
                    write_u32(&mut w, e.input_slot);
                    write_u32(&mut w, e.aligned_byte_offset);
                    unsafe { write_pod(&mut w, &e.input_slot_class); }
                    write_u32(&mut w, e.instance_data_step_rate);
                }
            }
            Recipe::Compute { root, cs } => {
                write_u8(&mut w, 3);
                write_bytes(&mut w, root);
                write_bytes(&mut w, cs);
            }
        }
        w
    }
    pub(crate) fn decode(mut r: &[u8]) -> Option<Self> {
        let tag = read_u8(&mut r)?;
        match tag {
            1 => {
                let root = read_bytes(&mut r)?; let ms = read_bytes(&mut r)?; let ps = read_bytes(&mut r)?;
                let state = GfxState::decode(&mut r)?;
                Some(Recipe::Mesh { root, ms, ps, state })
            }
            2 => {
                let root = read_bytes(&mut r)?; let vs = read_bytes(&mut r)?; let ps = read_bytes(&mut r)?;
                let has_gs = read_u8(&mut r)?;
                let gs = if has_gs == 1 { Some(read_bytes(&mut r)?) } else { None };
                let state = GfxState::decode(&mut r)?;
                let n = read_u32(&mut r)? as usize;
                let mut inputs = Vec::with_capacity(n);
                for _ in 0..n {
                    let name = read_bytes(&mut r)?;
                    let semantic_name = CString::from_vec_with_nul(name).ok()?;
                    let semantic_index = read_u32(&mut r)?;
                    let format = unsafe { read_pod(&mut r)? };
                    let input_slot = read_u32(&mut r)?;
                    let aligned_byte_offset = read_u32(&mut r)?;
                    let input_slot_class = unsafe { read_pod(&mut r)? };
                    let instance_data_step_rate = read_u32(&mut r)?;
                    inputs.push(InputElem { semantic_name, semantic_index, format, input_slot, aligned_byte_offset, input_slot_class, instance_data_step_rate });
                }
                Some(Recipe::Graphics { root, vs, ps, gs, state, inputs })
            }
            3 => {
                let root = read_bytes(&mut r)?; let cs = read_bytes(&mut r)?;
                Some(Recipe::Compute { root, cs })
            }
            _ => None,
        }
    }
}

fn bc(b: &[u8]) -> D3D12_SHADER_BYTECODE {
    D3D12_SHADER_BYTECODE { pShaderBytecode: b.as_ptr().cast(), BytecodeLength: b.len() }
}

fn make_root(device: &ID3D12Device, blob: &[u8]) -> windows::core::Result<ID3D12RootSignature> {
    unsafe { device.CreateRootSignature(0, blob) }
}

/// Child process: probe-create a pipeline. If it survives (no fastfail/abort),
/// write "ok" so the parent can create it in-process on its own device.
/// On clean failure, write "err:<msg>" for diagnostics.
pub fn run_helper(inp: &Path, out: &Path) -> Result<(), String> {
    let run = (|| -> Result<(), String> {
        let data = std::fs::read(inp).map_err(|e| e.to_string())?;
        let recipe = Recipe::decode(&data).ok_or("bad recipe")?;
        unsafe {
            let mut device: Option<ID3D12Device> = None;
            let hr = D3D12CreateDevice(None, D3D_FEATURE_LEVEL_11_0, &mut device);
            let device = if hr.is_ok() { device.ok_or("no device")? } else {
                use windows::Win32::Graphics::Dxgi::*;
                let factory: IDXGIFactory4 = CreateDXGIFactory2(DXGI_CREATE_FACTORY_FLAGS(0)).map_err(|e| e.to_string())?;
                let adapter: IDXGIAdapter = factory.EnumWarpAdapter().map_err(|e| e.to_string())?;
                let mut dev: Option<ID3D12Device> = None;
                D3D12CreateDevice(&adapter, D3D_FEATURE_LEVEL_11_0, &mut dev).map_err(|e| e.to_string())?;
                dev.ok_or("no warp device")?
            };
            let _pso = create_pso(&device, &recipe).map_err(|e| e.to_string())?;
            std::fs::write(out, b"ok").map_err(|e| e.to_string())?;
        }
        Ok(())
    })();
    if let Err(e) = &run {
        let _ = std::fs::write(out, format!("err:{e}").as_bytes());
    }
    run
}

unsafe fn create_pso(device: &ID3D12Device, recipe: &Recipe) -> windows::core::Result<ID3D12PipelineState> {
    match recipe {
        Recipe::Compute { root, cs } => {
            let root = unsafe { make_root(device, root)? };
            let desc = D3D12_COMPUTE_PIPELINE_STATE_DESC {
                pRootSignature: unsafe { std::mem::transmute_copy(&root) },
                CS: bc(cs),
                ..Default::default()
            };
            unsafe { device.CreateComputePipelineState(&desc) }
        }
        Recipe::Mesh { root, ms, ps, state } => {
            let root = unsafe { make_root(device, root)? };
            create_mesh_pso(device, &root, bc(ms), bc(ps), state)
        }
        Recipe::Graphics { root, vs, ps, gs, state, inputs } => {
            let root = unsafe { make_root(device, root)? };
            create_gfx_pso(device, &root, bc(vs), bc(ps), gs.as_deref().map(bc), state, inputs)
        }
    }
}

unsafe fn create_mesh_pso(
    device: &ID3D12Device, root: &ID3D12RootSignature,
    ms: D3D12_SHADER_BYTECODE, ps: D3D12_SHADER_BYTECODE, g: &GfxState,
) -> windows::core::Result<ID3D12PipelineState> {
    #[repr(C, align(8))]
    struct Sub<T>(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE, T);
    #[repr(C)]
    struct Stream {
        root: Sub<*mut std::ffi::c_void>,
        ms: Sub<D3D12_SHADER_BYTECODE>,
        ps: Sub<D3D12_SHADER_BYTECODE>,
        blend: Sub<D3D12_BLEND_DESC>,
        mask: Sub<u32>,
        raster: Sub<D3D12_RASTERIZER_DESC>,
        depth: Sub<D3D12_DEPTH_STENCIL_DESC>,
        targets: Sub<D3D12_RT_FORMAT_ARRAY>,
        dsv: Sub<DXGI_FORMAT>,
        samples: Sub<DXGI_SAMPLE_DESC>,
        topology: Sub<D3D12_PRIMITIVE_TOPOLOGY_TYPE>,
    }
    let mut stream = Stream {
        root: Sub(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_ROOT_SIGNATURE, unsafe { std::mem::transmute_copy(root) }),
        ms: Sub(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_MS, ms),
        ps: Sub(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PS, ps),
        blend: Sub(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_BLEND, g.blend),
        mask: Sub(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_MASK, g.sample_mask),
        raster: Sub(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RASTERIZER, g.rasterizer),
        depth: Sub(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL, g.depth),
        targets: Sub(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RENDER_TARGET_FORMATS, g.targets),
        dsv: Sub(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL_FORMAT, g.depth_format),
        samples: Sub(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_DESC, g.samples),
        topology: Sub(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PRIMITIVE_TOPOLOGY, D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE),
    };
    let desc = D3D12_PIPELINE_STATE_STREAM_DESC {
        SizeInBytes: std::mem::size_of::<Stream>(),
        pPipelineStateSubobjectStream: (&raw mut stream).cast(),
    };
    let device2: ID3D12Device2 = device.cast()?;
    unsafe { device2.CreatePipelineState(&desc) }
}

unsafe fn create_gfx_pso(
    device: &ID3D12Device, root: &ID3D12RootSignature,
    vs: D3D12_SHADER_BYTECODE, ps: D3D12_SHADER_BYTECODE, gs: Option<D3D12_SHADER_BYTECODE>,
    g: &GfxState, inputs: &[InputElem],
) -> windows::core::Result<ID3D12PipelineState> {
    let elems: Vec<D3D12_INPUT_ELEMENT_DESC> = inputs.iter().map(|e| D3D12_INPUT_ELEMENT_DESC {
        SemanticName: windows::core::PCSTR(e.semantic_name.as_ptr().cast()),
        SemanticIndex: e.semantic_index,
        Format: e.format,
        InputSlot: e.input_slot,
        AlignedByteOffset: e.aligned_byte_offset,
        InputSlotClass: e.input_slot_class,
        InstanceDataStepRate: e.instance_data_step_rate,
    }).collect();
    let layout = D3D12_INPUT_LAYOUT_DESC {
        pInputElementDescs: if elems.is_empty() { std::ptr::null() } else { elems.as_ptr() },
        NumElements: elems.len() as u32,
    };
    let mut desc = D3D12_GRAPHICS_PIPELINE_STATE_DESC {
        pRootSignature: unsafe { std::mem::transmute_copy(root) },
        VS: vs,
        PS: ps,
        BlendState: g.blend,
        SampleMask: g.sample_mask,
        RasterizerState: g.rasterizer,
        DepthStencilState: g.depth,
        InputLayout: layout,
        IBStripCutValue: g.strip_cut,
        PrimitiveTopologyType: g.topology,
        NumRenderTargets: g.targets.NumRenderTargets,
        DSVFormat: g.depth_format,
        SampleDesc: g.samples,
        ..Default::default()
    };
    desc.RTVFormats[..g.targets.RTFormats.len()].copy_from_slice(&g.targets.RTFormats);
    if let Some(gs) = gs { desc.GS = gs; }
    unsafe { device.CreateGraphicsPipelineState(&desc) }
}

/// Parent: probe pipeline creation in a child process. Returns Ok if the child
/// created the PSO without crashing (so parent can safely create it in-process).
/// A child crash/fastfail means the DXIL/driver kills the process - refuse it.
pub(crate) fn probe_isolated(recipe: &Recipe) -> windows::core::Result<()> {
    let exe = std::env::current_exe().map_err(|e| {
        tracing::error!(error=%e, "isolate: current_exe failed");
        windows::core::Error::new(windows::core::HRESULT(-1), e.to_string())
    })?;
    let dir = std::env::temp_dir().join("victory-pso");
    let _ = std::fs::create_dir_all(&dir);
    let id = std::process::id();
    let tid = std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).map(|d| d.as_nanos()).unwrap_or(0);
    let inp = dir.join(format!("{id}-{tid}.req"));
    let outp = dir.join(format!("{id}-{tid}.lib"));
    let errp = dir.join(format!("{id}-{tid}.err"));
    std::fs::write(&inp, recipe.encode()).map_err(|e| {
        tracing::error!(error=%e, "isolate: write req failed");
        windows::core::Error::new(windows::core::HRESULT(-1), e.to_string())
    })?;
    let errf = std::fs::File::create(&errp).ok();
    let mut cmd = std::process::Command::new(&exe);
    cmd.arg("_pipeline-helper").arg("--input").arg(&inp).arg("--output").arg(&outp);
    if let Some(f) = errf.as_ref() { cmd.stderr(f.try_clone().unwrap()); cmd.stdout(f.try_clone().unwrap()); }
    let status = cmd.status().map_err(|e| {
        tracing::error!(error=%e, exe=%exe.display(), "isolate: spawn helper failed");
        let _ = std::fs::remove_file(&inp);
        windows::core::Error::new(windows::core::HRESULT(-1), e.to_string())
    })?;
    let _ = std::fs::remove_file(&inp);
    let out_msg = std::fs::read_to_string(&outp).unwrap_or_default();
    let err = std::fs::read_to_string(&errp).unwrap_or_default();
    let _ = std::fs::remove_file(&outp);
    let _ = std::fs::remove_file(&errp);
    if !status.success() {
        tracing::warn!(?status, out=%out_msg, err=%err, "pipeline helper probe failed/crashed; refusing pipeline");
        return Err(windows::core::Error::from_hresult(windows::core::HRESULT(0x8000FFFFu32 as i32)));
    }
    if out_msg.trim() != "ok" {
        tracing::warn!(out=%out_msg, err=%err, "pipeline helper probe produced no ok marker; refusing pipeline");
        return Err(windows::core::Error::from_hresult(windows::core::HRESULT(-1)));
    }
    Ok(())
}

unsafe fn load_from_blob(
    device: &ID3D12Device,
    parent_lib: &ID3D12PipelineLibrary1,
    name: &PCWSTR,
    child_lib: &ID3D12PipelineLibrary1,
    recipe: &Recipe,
) -> windows::core::Result<ID3D12PipelineState> {
    let pso_name = HSTRING::from("pso");
    let pso_name_pcw = PCWSTR(pso_name.as_ptr());
    let pso = match recipe {
        Recipe::Compute { root, cs } => {
            let root = make_root(device, root)?;
            let desc = D3D12_COMPUTE_PIPELINE_STATE_DESC {
                pRootSignature: std::mem::transmute_copy(&root),
                CS: bc(cs),
                ..Default::default()
            };
            child_lib1_load_compute(child_lib, pso_name_pcw, &desc)?
        }
        Recipe::Mesh { root, ms, ps, state } => {
            let root = make_root(device, root)?;
            load_mesh_from_child(device, child_lib, pso_name_pcw, &root, bc(ms), bc(ps), state)?
        }
        Recipe::Graphics { root, vs, ps, gs, state, inputs } => {
            let root = make_root(device, root)?;
            let elems: Vec<D3D12_INPUT_ELEMENT_DESC> = inputs.iter().map(|e| D3D12_INPUT_ELEMENT_DESC {
                SemanticName: windows::core::PCSTR(e.semantic_name.as_ptr().cast()),
                SemanticIndex: e.semantic_index, Format: e.format, InputSlot: e.input_slot,
                AlignedByteOffset: e.aligned_byte_offset, InputSlotClass: e.input_slot_class,
                InstanceDataStepRate: e.instance_data_step_rate,
            }).collect();
            let layout = D3D12_INPUT_LAYOUT_DESC { pInputElementDescs: if elems.is_empty() { std::ptr::null() } else { elems.as_ptr() }, NumElements: elems.len() as u32 };
            let mut desc = D3D12_GRAPHICS_PIPELINE_STATE_DESC {
                pRootSignature: std::mem::transmute_copy(&root), VS: bc(vs), PS: bc(ps),
                BlendState: state.blend, SampleMask: state.sample_mask, RasterizerState: state.rasterizer,
                DepthStencilState: state.depth, InputLayout: layout, IBStripCutValue: state.strip_cut,
                PrimitiveTopologyType: state.topology, NumRenderTargets: state.targets.NumRenderTargets,
                DSVFormat: state.depth_format, SampleDesc: state.samples, ..Default::default()
            };
            desc.RTVFormats[..state.targets.RTFormats.len()].copy_from_slice(&state.targets.RTFormats);
            if let Some(gs) = gs { desc.GS = bc(gs); }
            child_lib.LoadGraphicsPipeline(pso_name_pcw, &desc)?
        }
    };
    // Store into parent library under the real name
    let _ = parent_lib.StorePipeline(*name, &pso);
    Ok(pso)
}

unsafe fn child_lib1_load_compute(lib: &ID3D12PipelineLibrary1, name: PCWSTR, desc: &D3D12_COMPUTE_PIPELINE_STATE_DESC) -> windows::core::Result<ID3D12PipelineState> {
    lib.LoadComputePipeline(name, desc)
}

unsafe fn load_mesh_from_child(
    device: &ID3D12Device, child_lib: &ID3D12PipelineLibrary1, name: PCWSTR,
    root: &ID3D12RootSignature, ms: D3D12_SHADER_BYTECODE, ps: D3D12_SHADER_BYTECODE, g: &GfxState,
) -> windows::core::Result<ID3D12PipelineState> {
    #[repr(C, align(8))]
    struct Sub<T>(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE, T);
    #[repr(C)]
    struct Stream {
        root: Sub<*mut std::ffi::c_void>,
        ms: Sub<D3D12_SHADER_BYTECODE>,
        ps: Sub<D3D12_SHADER_BYTECODE>,
        blend: Sub<D3D12_BLEND_DESC>,
        mask: Sub<u32>,
        raster: Sub<D3D12_RASTERIZER_DESC>,
        depth: Sub<D3D12_DEPTH_STENCIL_DESC>,
        targets: Sub<D3D12_RT_FORMAT_ARRAY>,
        dsv: Sub<DXGI_FORMAT>,
        samples: Sub<DXGI_SAMPLE_DESC>,
        topology: Sub<D3D12_PRIMITIVE_TOPOLOGY_TYPE>,
    }
    let mut stream = Stream {
        root: Sub(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_ROOT_SIGNATURE, std::mem::transmute_copy(root)),
        ms: Sub(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_MS, ms),
        ps: Sub(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PS, ps),
        blend: Sub(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_BLEND, g.blend),
        mask: Sub(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_MASK, g.sample_mask),
        raster: Sub(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RASTERIZER, g.rasterizer),
        depth: Sub(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL, g.depth),
        targets: Sub(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RENDER_TARGET_FORMATS, g.targets),
        dsv: Sub(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL_FORMAT, g.depth_format),
        samples: Sub(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_DESC, g.samples),
        topology: Sub(D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PRIMITIVE_TOPOLOGY, D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE),
    };
    let desc = D3D12_PIPELINE_STATE_STREAM_DESC { SizeInBytes: std::mem::size_of::<Stream>(), pPipelineStateSubobjectStream: (&raw mut stream).cast() };
    child_lib.LoadPipeline(name, &desc)
}
