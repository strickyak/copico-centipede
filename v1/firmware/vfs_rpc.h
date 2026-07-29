#ifndef FIRMWARE_PIO_VFS_RPC_H_
#define FIRMWARE_PIO_VFS_RPC_H_

#define RPC_VERBOSE 0

#include <string>
#include <vector>

#include "coro.h"
#include "pcb.h"

#define T_RPC 180

extern void PumpUsbCobs();

extern "C" {
extern int putchar_raw(int c);
}

#include "cobs_tx.h"

namespace rpc {

inline void send_rpc(const pcb::RpcRequest& req) {
#ifdef RPC_VERBOSE
  cobs_printf("{r%d:%s ", req.serial, req.method.c_str());
#endif
  std::vector<uint8_t> payload = req.encode();
  unsigned char* pkt = new unsigned char[payload.size() + 1];
  pkt[0] = T_RPC;
  for (size_t i = 0; i < payload.size(); i++) {
    pkt[i + 1] = payload[i];
  }
  CobsEncodeAndTransmit(pkt, payload.size() + 1, putchar_raw);
  delete[] pkt;
}

extern bool rpc_response_ready;
extern pcb::RpcResponse last_rpc_response;
extern int next_serial;

inline pcb::RpcResponse vfs_rpc_call(const pcb::RpcRequest& req, Coro* self = nullptr) {
  if (!usb_tether_ok()) {
    // No USB tether: cannot perform RPC to host.
    // Return an error response so the VFS layer propagates the failure.
    pcb::RpcResponse err_resp;
    err_resp.status = -1;
    err_resp.serial = req.serial;
    return err_resp;
  }
  rpc_response_ready = false;
  send_rpc(req);

  // Block until we get a response
  while (!rpc_response_ready) {
    PumpUsbCobs();
    if (self) {
      coro_yield(self);
    }
  }

#ifdef RPC_VERBOSE
  cobs_printf(" r%d}", req.serial);
#endif
  return last_rpc_response;
}

}  // namespace rpc

#endif  // FIRMWARE_PIO_VFS_RPC_H_
