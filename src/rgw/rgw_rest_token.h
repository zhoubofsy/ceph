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

#ifndef RGW_REST_TOKEN_H
#define RGW_REST_TOKEN_H

#include "rgw_rest_s3.h"
#include "rgw_common.h"

//#define dout_subsys ceph_subsys_rgw

struct RGWInnerTokenOpState {
  RGWUserInfo *user;
  rgwinner::Token token;
  rgwinner::TokenMgr token_mgr;
};

class RGWOp_Token_Create : public RGWRESTOp {
private:
  std::string get_access_key(const struct req_info* info);
public:
  RGWOp_Token_Create() {}
  ~RGWOp_Token_Create() override {}

  int check_caps(RGWUserCaps& caps) override {
    return 0;
  }
  void execute() override;
  const string name() override {
    return "create_token";
  }
};

class RGWHandler_Token : public RGWHandler_Auth_S3 {
protected:
  RGWOp *op_put() override;

public:
  using RGWHandler_Auth_S3::RGWHandler_Auth_S3;
  ~RGWHandler_Token() override = default;

  int read_permissions(RGWOp *) override {
      return 0;
  }
};

class RGWRESTMgr_Token : public RGWRESTMgr {
public:
  RGWRESTMgr_Token() = default;
  ~RGWRESTMgr_Token() override = default;

  RGWHandler_REST* get_handler(struct req_state*,
                               const rgw::auth::StrategyRegistry& auth_registry,
                               const std::string&) override {
      return new RGWHandler_Token(auth_registry);
  }
};

#endif
