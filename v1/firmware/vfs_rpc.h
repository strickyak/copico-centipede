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

#include "cobs_tx.h"

namespace rpc {

inline void send_rpc(const pcb::RpcRequest& req) {
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
