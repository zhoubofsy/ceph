// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*- 
// vim: ts=8 sw=2 smarttab
/*
 * Ceph - scalable distributed file system
 *
 * Copyright (C) 2013 eNovance SAS <licensing@enovance.com>
 *
 * This is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License version 2.1, as published by the Free Software
 * Foundation. See file COPYING.
 *
 */

#include "rgw_rest_token.h"

#define dout_subsys ceph_subsys_rgw

RGWOp *RGWHandler_Token::op_put() {
    return new RGWOp_Token_Create;
}

class RGWInnerTokenOp
{
  std::string token_b64;
  std::string token_id;
public:
  int create(RGWRados *store, RGWInnerTokenOpState *op_state, RGWFormatterFlusher& flusher);
  int remove(RGWRados *store, RGWInnerTokenOpState *op_state, RGWFormatterFlusher& flusher);
  void dump(Formatter *f) const {
    ::encode_json("token", token_b64, f);
  }
};


int RGWInnerTokenOp::create(RGWRados *store, RGWInnerTokenOpState *op_state, RGWFormatterFlusher& flusher) {
  int ret;
  // Compare token count and max-tokens, if token count bigger than max-tokens then return overflow.
  if (op_state->user->max_tokens <= op_state->token_mgr.count()) {
    return -EMLINK;
  }
  // notify token garbage dispose
  float tokens_ratio = (float)(op_state->token_mgr.count()) / (float)(op_state->user->max_tokens);
  if (tokens_ratio >= store->ctx()->_conf->rgw_gd_token_processor_ratio)
    store->wakeup_token_gd();

  // Create Token , Calculate token_id and save it to a object of ceph.
  token_b64 = op_state->token.encode_json_base64();
  token_id = op_state->token.generate_key(token_b64);
  ldout(store->ctx(), 20) << "token base64: " << token_b64 << dendl;
  ldout(store->ctx(), 20) << "token id: " << token_id << dendl;
  // Update token mgr.
  ret = op_state->token_mgr.add_token(token_id, op_state->token);
  if(ret == 0)
    ::encode_json("token", *this, flusher.get_formatter());
  return ret;
}

int RGWInnerTokenOp::remove(RGWRados *store, RGWInnerTokenOpState *op_state, RGWFormatterFlusher& flusher) {
  return 0;
}

void RGWOp_Token_Create::execute() {
  uint64_t expire;
  std::string type;

  RESTArgs::get_string(s, "token-type", type, &type);
  //RESTArgs::get_uint64(s, "token-expire", expire, &expire);
  expire = s->user->token_valid_tm;

  RGWInnerTokenOpState op_state;
  // Get RGWUserInfo
  op_state.user = s->user;
  // Create and get token mgr of current user
  int ret = op_state.token_mgr.init(store, &(s->user->user_id));
  if(!(ret == 0 || ret == -ENOENT)) {
    ldout(store->ctx(), 0) << "ERROR: failed to init token mgr : " << ret << dendl;
    http_ret = ret;
    return;
  }
  // parse access_key
  std::string access_key = this->get_access_key(&(s->info));
  op_state.token.init(store, access_key);
  op_state.token.type = rgwinner::Token::to_type(type);
  op_state.token.expire = expire;
  RGWInnerTokenOp inner_token_op;
  http_ret = inner_token_op.create(store, &op_state, flusher);
}

std::string RGWOp_Token_Create::get_access_key(const struct req_info* info) {
  std::string access_key_id;
  const char* http_auth = info->env->get("HTTP_AUTHORIZATION");
  if (! http_auth || http_auth[0] == '\0') {
    access_key_id = info->args.get("AWSAccessKeyId");
  } else {
    const boost::string_view auth_str(http_auth + strlen("AWS "));
    const size_t pos = auth_str.rfind(':');
    if (pos != boost::string_view::npos) {
      //access_key_id = auth_str.substr(0, pos);
      char ak[128] = {0};
      auth_str.copy(ak, pos, 0);
      access_key_id = ak;
    }
  }
  return access_key_id;
}

