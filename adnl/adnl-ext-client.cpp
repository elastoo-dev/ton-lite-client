/*
    This file is part of TON Blockchain Library.

    TON Blockchain Library is free software: you can redistribute it and/or modify
    it under the terms of the GNU Lesser General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.

    TON Blockchain Library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public License
    along with TON Blockchain Library.  If not, see <http://www.gnu.org/licenses/>.

    Copyright 2017-2019 Telegram Systems LLP
*/
#include "adnl-ext-client.hpp"
#include "adnl-ext-client.h"

namespace ton {

namespace adnl {

void AdnlExtClientImpl::alarm() {
  if (conn_.empty() || !conn_.is_alive()) {
    next_create_at_ = td::Timestamp::in(10.0);
    alarm_timestamp() = next_create_at_;

    auto fd = td::SocketFd::open(dst_addr_);
    if (fd.is_error()) {
      LOG(INFO) << "failed to connect to " << dst_addr_ << ": " << fd.move_as_error();
      return;
    }

    class Cb : public AdnlExtConnection::Callback {
     private:
      td::actor::ActorId<AdnlExtClientImpl> id_;

     public:
      void on_ready(td::actor::ActorId<AdnlExtConnection> conn) {
        td::actor::send_closure(id_, &AdnlExtClientImpl::conn_ready, conn);
      }
      void on_close(td::actor::ActorId<AdnlExtConnection> conn) {
        td::actor::send_closure(id_, &AdnlExtClientImpl::conn_stopped, conn);
      }
      Cb(td::actor::ActorId<AdnlExtClientImpl> id) : id_(id) {
      }
    };

    conn_ = td::actor::create_actor<AdnlOutboundConnection>(td::actor::ActorOptions().with_name("outconn").with_poll(),
                                                            fd.move_as_ok(), std::make_unique<Cb>(actor_id(this)), dst_,
                                                            local_id_, actor_id(this));
  }
}

td::Status AdnlOutboundConnection::process_custom_packet(td::BufferSlice &data, bool &processed) {
  if (data.size() == 12) {
    auto F = fetch_tl_object<ton_api::tcp_pong>(data.clone(), true);
    if (F.is_ok()) {
      processed = true;
      return td::Status::OK();
    }
  }
  if (!local_id_.empty() && nonce_.size() != 0) {
    auto F = fetch_tl_object<ton_api::tcp_authentificationNonce>(data.clone(), true);
    if (F.is_ok()) {
      auto f = F.move_as_ok();
      if (f->nonce_.size() == 0 || f->nonce_.size() > 512) {
        return td::Status::Error(ErrorCode::protoviolation, "bad nonce size");
      }
      td::SecureString ss{nonce_.size() + f->nonce_.size()};
      ss.as_mutable_slice().copy_from(nonce_.as_slice());
      ss.as_mutable_slice().remove_prefix(nonce_.size()).copy_from(f->nonce_.as_slice());

      TRY_RESULT(dec, local_id_.create_decryptor());
      TRY_RESULT(B, dec->sign(ss.as_slice()));

      auto obj =
          create_tl_object<ton_api::tcp_authentificationComplete>(local_id_.compute_public_key().tl(), std::move(B));
      send(serialize_tl_object(obj, true));

      nonce_.clear();

      processed = true;
      return td::Status::OK();
    }
  }
  return td::Status::OK();
}

void AdnlOutboundConnection::start_up() {
  AdnlExtConnection::start_up();
  auto X = dst_.pubkey().create_encryptor();
  if (X.is_error()) {
    LOG(ERROR) << "failed to init encryptor: " << X.move_as_error();
    stop();
    return;
  }
  auto enc = X.move_as_ok();

  td::BufferSlice d{256};
  auto id = dst_.compute_short_id();
  auto S = d.as_slice();
  S.copy_from(id.as_slice());
  S.remove_prefix(32);
  S.truncate(256 - 64 - 32);
  td::Random::secure_bytes(S);
  init_crypto(S);

  auto R = enc->encrypt(S);
  if (R.is_error()) {
    LOG(ERROR) << "failed to  encrypt: " << R.move_as_error();
    stop();
    return;
  }
  auto data = R.move_as_ok();
  LOG_CHECK(data.size() == 256 - 32) << "size=" << data.size();
  S = d.as_slice();
  S.remove_prefix(32);
  CHECK(S.size() == data.size());
  S.copy_from(data.as_slice());

  send_uninit(std::move(d));

  if (!local_id_.empty()) {
    nonce_ = td::SecureString{32};
    td::Random::secure_bytes(nonce_.as_mutable_slice());
    auto obj = create_tl_object<ton_api::tcp_authentificate>(td::BufferSlice{nonce_.as_slice()});
    send(serialize_tl_object(obj, true));
  }
}

void AdnlExtClientImpl::check_ready(td::Promise<td::Unit> promise) {
  if (conn_.empty() || !conn_.is_alive()) {
    promise.set_error(td::Status::Error(ErrorCode::notready, "not ready"));
    return;
  }
  td::actor::send_closure(td::actor::ActorId<AdnlExtConnection>{conn_.get()}, &AdnlExtConnection::check_ready_async,
                          std::move(promise));
}

td::actor::ActorOwn<AdnlExtClient> AdnlExtClient::create(AdnlNodeIdFull dst, td::IPAddress dst_addr,
                                                         std::unique_ptr<AdnlExtClient::Callback> callback) {
  return td::actor::create_actor<AdnlExtClientImpl>("extclient", std::move(dst), dst_addr, std::move(callback));
}

td::actor::ActorOwn<AdnlExtClient> AdnlExtClient::create(AdnlNodeIdFull dst, PrivateKey local_id,
                                                         td::IPAddress dst_addr,
                                                         std::unique_ptr<AdnlExtClient::Callback> callback) {
  return td::actor::create_actor<AdnlExtClientImpl>("extclient", std::move(dst), std::move(local_id), dst_addr,
                                                    std::move(callback));
}

td::Status AdnlOutboundConnection::process_packet(td::BufferSlice data) {
  TRY_RESULT(F, fetch_tl_object<lite_api::adnl_message_answer>(std::move(data), true));
  td::actor::send_closure(ext_client_, &AdnlExtClientImpl::answer_query, F->query_id_, std::move(F->answer_));
  return td::Status::OK();
}

}  // namespace adnl

}  // namespace ton
