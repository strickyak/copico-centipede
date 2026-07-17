#ifndef FIRMWARE_PIO_VFS_RPC_H_
#define FIRMWARE_PIO_VFS_RPC_H_

#include <string>
#include <vector>

#include "pcb.h"

#define T_RPC 180

extern void PumpUsbCobs();

extern "C" {
extern int putchar_raw(int c);
}

namespace rpc {

inline void putchar_size(size_t sz) {
  if (sz < 64) {
    putchar_raw(static_cast<int>(sz | 128));
  } else {
    putchar_raw(static_cast<int>((sz / 64) | 192));
    putchar_raw(static_cast<int>((sz % 64) | 128));
  }
}

inline void send_rpc(const pcb::RpcRequest& req) {
  std::vector<uint8_t> payload = req.encode();
  putchar_raw(T_RPC);
  putchar_size(payload.size());
  for (uint8_t b : payload) {
    putchar_raw(b);
  }
}

extern bool rpc_response_ready;
extern pcb::RpcResponse last_rpc_response;
extern int next_serial;

inline pcb::RpcResponse vfs_rpc_call(const pcb::RpcRequest& req) {
  rpc_response_ready = false;
  send_rpc(req);

  // Block until we get a response
  while (!rpc_response_ready) {
    PumpUsbCobs();
  }

  return last_rpc_response;
}

}  // namespace rpc

#endif  // FIRMWARE_PIO_VFS_RPC_H_
