//! Probe: does `OpenExistingHeapFromAddress1` see the CPU's memory, both for
//! private memory and for a view of a committed section (how xmem backs the
//! title's physical pages under Wine)? Writes a pattern on the CPU, has the
//! GPU copy it to a readback buffer, then has the GPU write into the heap and
//! reads it back on the CPU.
#[cfg(windows)]
fn main() {
    use windows::Win32::Graphics::Direct3D::D3D_FEATURE_LEVEL_12_0;
    use windows::Win32::Graphics::Direct3D12::*;
    use windows::Win32::Graphics::Dxgi::Common::*;
    use windows::Win32::System::Threading::{CreateEventW, WaitForSingleObject, INFINITE};
    use windows::core::Interface;
    use windows_sys::Win32::Foundation::INVALID_HANDLE_VALUE;
    use windows_sys::Win32::System::Memory::*;

    const LEN: usize = 1 << 20;
    unsafe {
        let mut dev: Option<ID3D12Device> = None;
        D3D12CreateDevice(None, D3D_FEATURE_LEVEL_12_0, &mut dev).unwrap();
        let dev = dev.unwrap();
        let dev13: ID3D12Device13 = dev.cast().expect("ID3D12Device13");
        let queue: ID3D12CommandQueue = dev
            .CreateCommandQueue(&D3D12_COMMAND_QUEUE_DESC { Type: D3D12_COMMAND_LIST_TYPE_DIRECT, ..Default::default() })
            .unwrap();
        let alloc: ID3D12CommandAllocator = dev.CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT).unwrap();
        let fence: ID3D12Fence = dev.CreateFence(0, D3D12_FENCE_FLAG_NONE).unwrap();
        let mut fv = 0u64;
        let buf_desc = |w: u64, flags| D3D12_RESOURCE_DESC {
            Dimension: D3D12_RESOURCE_DIMENSION_BUFFER, Width: w, Height: 1, DepthOrArraySize: 1, MipLevels: 1,
            Format: DXGI_FORMAT_UNKNOWN, SampleDesc: DXGI_SAMPLE_DESC { Count: 1, Quality: 0 },
            Layout: D3D12_TEXTURE_LAYOUT_ROW_MAJOR, Flags: flags, ..Default::default()
        };
        let mut readback: Option<ID3D12Resource> = None;
        dev.CreateCommittedResource(&D3D12_HEAP_PROPERTIES { Type: D3D12_HEAP_TYPE_READBACK, ..Default::default() },
            D3D12_HEAP_FLAG_NONE, &buf_desc(LEN as u64, D3D12_RESOURCE_FLAG_NONE), D3D12_RESOURCE_STATE_COPY_DEST, None, &mut readback).unwrap();
        let readback = readback.unwrap();
        let mut upload: Option<ID3D12Resource> = None;
        dev.CreateCommittedResource(&D3D12_HEAP_PROPERTIES { Type: D3D12_HEAP_TYPE_UPLOAD, ..Default::default() },
            D3D12_HEAP_FLAG_NONE, &buf_desc(LEN as u64, D3D12_RESOURCE_FLAG_NONE), D3D12_RESOURCE_STATE_GENERIC_READ, None, &mut upload).unwrap();
        let upload = upload.unwrap();
        let mut up: *mut std::ffi::c_void = std::ptr::null_mut();
        upload.Map(0, None, Some(&mut up)).unwrap();
        std::ptr::write_bytes(up.cast::<u8>(), 0x5A, LEN);

        let mut run = |f: &dyn Fn(&ID3D12GraphicsCommandList)| {
            alloc.Reset().unwrap();
            let list: ID3D12GraphicsCommandList = dev.CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, &alloc, None).unwrap();
            f(&list);
            list.Close().unwrap();
            queue.ExecuteCommandLists(&[Some(list.cast().unwrap())]);
            fv += 1;
            queue.Signal(&fence, fv).unwrap();
            let ev = CreateEventW(None, false, false, None).unwrap();
            fence.SetEventOnCompletion(fv, ev).unwrap();
            WaitForSingleObject(ev, INFINITE);
        };

        let mut probe = |what: &str, mem: *mut u8| {
            std::ptr::write_bytes(mem, 0xA5, LEN);
            let heap: windows::core::Result<ID3D12Heap> = dev13.OpenExistingHeapFromAddress1(mem as *const _, LEN);
            let heap = match heap { Ok(h) => h, Err(e) => { println!("{what}: heap refused: {e}"); return; } };
            let mut res: Option<ID3D12Resource> = None;
            dev.CreatePlacedResource(&heap, 0, &buf_desc(LEN as u64, D3D12_RESOURCE_FLAG_ALLOW_CROSS_ADAPTER), D3D12_RESOURCE_STATE_COMMON, None, &mut res).unwrap();
            let res = res.unwrap();
            run(&|l| l.CopyBufferRegion(&readback, 0, &res, 0, LEN as u64));
            let mut rb: *mut std::ffi::c_void = std::ptr::null_mut();
            readback.Map(0, None, Some(&mut rb)).unwrap();
            let gpu_saw = *rb.cast::<u8>().add(LEN / 2);
            readback.Unmap(0, None);
            run(&|l| l.CopyBufferRegion(&res, 0, &upload, 0, LEN as u64));
            let cpu_sees = std::ptr::read_volatile(mem.add(LEN / 2));
            println!("{what}: GPU read CPU data {} (0x{gpu_saw:02x}, want 0xa5); CPU sees GPU write {} (0x{cpu_sees:02x}, want 0x5a)",
                if gpu_saw == 0xA5 { "OK" } else { "WRONG" }, if cpu_sees == 0x5A { "OK" } else { "WRONG" });
        };

        let private = VirtualAlloc(std::ptr::null(), LEN, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE).cast::<u8>();
        probe("private VirtualAlloc", private);
        for (name, flag) in [("section SEC_COMMIT", SEC_COMMIT), ("section SEC_RESERVE+commit", SEC_RESERVE)] {
            let h = CreateFileMappingW(INVALID_HANDLE_VALUE, std::ptr::null(), PAGE_READWRITE | flag, 0, LEN as u32, std::ptr::null());
            let view = MapViewOfFile(h, FILE_MAP_ALL_ACCESS, 0, 0, LEN).Value.cast::<u8>();
            if flag == SEC_RESERVE { VirtualAlloc(view.cast(), LEN, MEM_COMMIT, PAGE_READWRITE); }
            probe(name, view);
        }
    }
}
#[cfg(not(windows))]
fn main() {}
