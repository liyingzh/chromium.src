// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_RENDERER_GPU_GPU_CHANNEL_HOST_H_
#define CONTENT_RENDERER_GPU_GPU_CHANNEL_HOST_H_
#pragma once

#include <string>
#include <vector>

#include "base/hash_tables.h"
#include "base/memory/scoped_ptr.h"
#include "base/process_util.h"
#include "content/common/gpu/gpu_info.h"
#include "content/common/message_router.h"
#include "content/renderer/gpu/gpu_video_decode_accelerator_host.h"
#include "ipc/ipc_channel_handle.h"
#include "ipc/ipc_sync_channel.h"
#include "ui/gfx/native_widget_types.h"
#include "ui/gfx/size.h"

class CommandBufferProxy;
class GpuSurfaceProxy;
class GURL;
class TransportTextureService;

// Encapsulates an IPC channel between the renderer and one plugin process.
// On the plugin side there's a corresponding GpuChannel.
class GpuChannelHost : public IPC::Channel::Listener,
                       public IPC::Message::Sender,
                       public base::RefCountedThreadSafe<GpuChannelHost> {
 public:
  enum State {
    // Not yet connected.
    kUnconnected,
    // Ready to use.
    kConnected,
    // An error caused the host to become disconnected. Recreate channel to
    // reestablish connection.
    kLost
  };

  // Called on the render thread
  GpuChannelHost();
  virtual ~GpuChannelHost();

  // Connect to GPU process channel.
  void Connect(const IPC::ChannelHandle& channel_handle,
               base::ProcessHandle renderer_process_for_gpu);

  State state() const { return state_; }

  // Change state to kLost.
  void SetStateLost();

  // The GPU stats reported by the GPU process.
  void set_gpu_info(const GPUInfo& gpu_info);
  const GPUInfo& gpu_info() const;

  // IPC::Channel::Listener implementation:
  virtual bool OnMessageReceived(const IPC::Message& msg);
  virtual void OnChannelConnected(int32 peer_pid);
  virtual void OnChannelError();

  // IPC::Message::Sender implementation:
  virtual bool Send(IPC::Message* msg);

  // Create and connect to a command buffer in the GPU process.
  CommandBufferProxy* CreateViewCommandBuffer(
      int render_view_id,
      const std::string& allowed_extensions,
      const std::vector<int32>& attribs,
      const GURL& active_url);

  // Create and connect to a command buffer in the GPU process.
  CommandBufferProxy* CreateOffscreenCommandBuffer(
      const gfx::Size& size,
      const std::string& allowed_extensions,
      const std::vector<int32>& attribs,
      const GURL& active_url);

  // Creates a video decoder in the GPU process.
  // Returned pointer is owned by the CommandBufferProxy for |route_id|.
  GpuVideoDecodeAcceleratorHost* CreateVideoDecoder(
      int command_buffer_route_id,
      const std::vector<uint32>& configs,
      media::VideoDecodeAccelerator::Client* client);

  // Destroy a command buffer created by this channel.
  void DestroyCommandBuffer(CommandBufferProxy* command_buffer);

  // Create a surface in the GPU process. Returns null on failure.
  GpuSurfaceProxy* CreateOffscreenSurface(const gfx::Size& size);

  // Destroy a surface in the GPU process.
  void DestroySurface(GpuSurfaceProxy* surface);

  TransportTextureService* transport_texture_service() {
    return transport_texture_service_.get();
  }

 private:
  State state_;

  GPUInfo gpu_info_;

  scoped_ptr<IPC::SyncChannel> channel_;

  // Used to implement message routing functionality to CommandBufferProxy
  // objects
  MessageRouter router_;

  // Keep track of all the registered CommandBufferProxies to
  // inform about OnChannelError
  typedef base::hash_map<int, CommandBufferProxy*> ProxyMap;
  ProxyMap proxies_;

  // This is a MessageFilter to intercept IPC messages related to transport
  // textures. These messages are routed to TransportTextureHost.
  scoped_refptr<TransportTextureService> transport_texture_service_;

  DISALLOW_COPY_AND_ASSIGN(GpuChannelHost);
};

#endif  // CONTENT_RENDERER_GPU_GPU_CHANNEL_HOST_H_
