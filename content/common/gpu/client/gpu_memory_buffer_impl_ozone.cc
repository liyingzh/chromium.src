// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/common/gpu/client/gpu_memory_buffer_impl.h"

#include "content/common/gpu/client/gpu_memory_buffer_impl_ozone_native_buffer.h"
#include "content/common/gpu/client/gpu_memory_buffer_impl_shared_memory.h"

namespace content {

// static
void GpuMemoryBufferImpl::GetSupportedTypes(
    std::vector<gfx::GpuMemoryBufferType>* types) {
  const gfx::GpuMemoryBufferType supported_types[] = {
    gfx::OZONE_NATIVE_BUFFER,
    gfx::SHARED_MEMORY_BUFFER
  };
  types->assign(supported_types, supported_types + arraysize(supported_types));
}

// static
bool GpuMemoryBufferImpl::IsConfigurationSupported(
    gfx::GpuMemoryBufferType type,
    Format format,
    Usage usage) {
  switch (type) {
    case gfx::SHARED_MEMORY_BUFFER:
      return GpuMemoryBufferImplSharedMemory::IsFormatSupported(format) &&
             GpuMemoryBufferImplSharedMemory::IsUsageSupported(usage);
    case gfx::OZONE_NATIVE_BUFFER:
      return GpuMemoryBufferImplOzoneNativeBuffer::IsFormatSupported(format) &&
             GpuMemoryBufferImplOzoneNativeBuffer::IsUsageSupported(usage);
    default:
      NOTREACHED();
      return false;
  }
}

// static
void GpuMemoryBufferImpl::Create(gfx::GpuMemoryBufferType type,
                                 gfx::GpuMemoryBufferId id,
                                 const gfx::Size& size,
                                 Format format,
                                 Usage usage,
                                 int client_id,
                                 const CreationCallback& callback) {
  switch (type) {
    case gfx::SHARED_MEMORY_BUFFER:
      GpuMemoryBufferImplSharedMemory::Create(id, size, format, callback);
      break;
    case gfx::OZONE_NATIVE_BUFFER:
      GpuMemoryBufferImplOzoneNativeBuffer::Create(
          id, size, format, client_id, callback);
      break;
    default:
      NOTREACHED();
      break;
  }
}

// static
void GpuMemoryBufferImpl::AllocateForChildProcess(
    gfx::GpuMemoryBufferType type,
    gfx::GpuMemoryBufferId id,
    const gfx::Size& size,
    Format format,
    Usage usage,
    base::ProcessHandle child_process,
    int child_client_id,
    const AllocationCallback& callback) {
  switch (type) {
    case gfx::SHARED_MEMORY_BUFFER:
      GpuMemoryBufferImplSharedMemory::AllocateForChildProcess(
          id, size, format, child_process, callback);
      break;
    case gfx::OZONE_NATIVE_BUFFER:
      GpuMemoryBufferImplOzoneNativeBuffer::AllocateForChildProcess(
          id, size, format, child_client_id, callback);
      break;
    default:
      NOTREACHED();
      break;
  }
}

// static
void GpuMemoryBufferImpl::DeletedByChildProcess(
    gfx::GpuMemoryBufferType type,
    gfx::GpuMemoryBufferId id,
    base::ProcessHandle child_process,
    int child_client_id,
    uint32_t sync_point) {
  switch (type) {
    case gfx::SHARED_MEMORY_BUFFER:
      break;
    case gfx::OZONE_NATIVE_BUFFER:
      GpuMemoryBufferImplOzoneNativeBuffer::DeletedByChildProcess(
          id, child_client_id, sync_point);
      break;
    default:
      NOTREACHED();
      break;
  }
}

// static
scoped_ptr<GpuMemoryBufferImpl> GpuMemoryBufferImpl::CreateFromHandle(
    const gfx::GpuMemoryBufferHandle& handle,
    const gfx::Size& size,
    Format format,
    const DestructionCallback& callback) {
  switch (handle.type) {
    case gfx::SHARED_MEMORY_BUFFER:
      return GpuMemoryBufferImplSharedMemory::CreateFromHandle(
          handle, size, format, callback);
    case gfx::OZONE_NATIVE_BUFFER:
      return GpuMemoryBufferImplOzoneNativeBuffer::CreateFromHandle(
          handle, size, format, callback);
    default:
      NOTREACHED();
      return scoped_ptr<GpuMemoryBufferImpl>();
  }
}

}  // namespace content
