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

#include "rgw_common.h"
#include "rgw_tools.h"
#include "rgw_rados.h"
#include "rgw_inner_token.h"

#define dout_subsys ceph_subsys_rgw

static int inner_token_lock_timeout = 30; // second

int rgw_get_token(RGWRados *store, boost::string_view tid, rgwinner::Token& token_info) {
  real_time mtime;
  bufferlist bl;
  RGWObjectCtx obj_ctx(store);

  int ret = rgw_get_system_obj(store, obj_ctx, store->get_zone_params().user_token_pool, tid.to_string(), bl, NULL, &mtime);
  if(ret < 0) {
    return ret;
  }

  bufferlist::iterator iter = bl.begin();
  try{
    if(!iter.end()) {
      decode(token_info, iter);
    }
  } catch (buffer::error& err) {
    ldout(store->ctx(), 0) << "ERROR: failed to decode token info, caught buffer::error" << dendl;
    return -EIO;
  }
  return ret;
}

int rgw_store_token(RGWRados *store, boost::string_view tid, const rgwinner::Token& token) {
  bufferlist bl;
  std::string oid = tid.to_string();
  encode(token, bl);

  int ret = rgw_put_system_obj(store, store->get_zone_params().user_token_pool, oid, bl, true, NULL, real_time());
  if(ret < 0)
    ldout(store->ctx(), 0) << "ERROR: failed to persist token. Error: " << ret << dendl;
  return ret;
}

int rgw_remove_token(RGWRados *store, boost::string_view tid) {
  int ret = rgw_delete_system_obj(store, store->get_zone_params().user_token_pool, tid.to_string(), NULL);
  if (ret == -ENOENT) {
    ret = 0;
  }
  else if(ret < 0) {
    ldout(store->ctx(), 0) << "ERROR, failed to delete token object [" << tid << "]. Error: " << ret << dendl;
  }
  return ret;
}

int rgw_store_token_mgr(RGWRados *store,
                        const rgw_user& uid,
                        rgwinner::TokenMgr& tknmgr) {
  bufferlist bl;
  std::string oid = uid.to_str();
  encode(tknmgr, bl);

  //int ret_tmp = rgw_delete_system_obj(store, store->get_zone_params().user_token_pool, oid, NULL);
  int ret = rgw_put_system_obj(store, store->get_zone_params().user_token_pool, oid, bl, false, NULL, real_time());
  if(ret < 0)
    ldout(store->ctx(), 0) << "ERROR: failed to persist token mgr. Error: " << ret << dendl;

  return ret;
}

int rgw_get_token_mgr_by_uid(RGWRados *store,
                             const rgw_user& uid,
                             rgwinner::TokenMgr& tknmgr) {
  real_time mtime;
  bufferlist bl;
  RGWObjectCtx obj_ctx(store);
  string oid = uid.to_str();
  int ret = rgw_get_system_obj(store, obj_ctx, store->get_zone_params().user_token_pool, oid, bl, NULL, &mtime);
  if(ret < 0) {
    return ret;
  }

  bufferlist::iterator iter = bl.begin();
  try{
    if(!iter.end()) {
      decode(tknmgr, iter);
    }
  } catch (buffer::error& err) {
    ldout(store->ctx(), 0) << "ERROR: failed to decode token mgr, caught buffer::error" << dendl;
    return -EIO;
  }

  return 0;
}

int rgw_token_mgr_add_tid(RGWRados *store, rgw_user& uid, boost::string_view tid) {
  int ret;
  rgwinner::TokenLock l;
  rgwinner::TokenMgr token_mgr;

  l.init(store, uid);
  l.lock();
  ldout(store->ctx(), 20) << "token mgr add token id LOCK! " << tid << dendl;
  // load token mgr
  ret = rgw_get_token_mgr_by_uid(store, uid, token_mgr);
  if(ret == 0 || ret == -ENOENT) {
    if(ret == -ENOENT) {
      token_mgr.init(store, &uid);
    }
    token_mgr.add_tid(tid);
    // reback
    ret = rgw_store_token_mgr(store, uid, token_mgr);
  } else {
    ldout(store->ctx(), 0) << "ERROR: failed to read token mgr object [" << uid.to_str() << "]. Error: " << ret << dendl;
  }
  ldout(store->ctx(), 20) << "token mgr add token id UNLOCK! " << tid << dendl;
  l.unlock();

  return ret;
}

int rgw_token_mgr_del_tid(RGWRados *store, rgw_user& uid, boost::string_view tid) {
  int ret;
  rgwinner::TokenLock l;
  rgwinner::TokenMgr token_mgr;

  l.init(store, uid);
  l.lock();
  ldout(store->ctx(), 20) << "token mgr delete token id LOCK! " << tid << dendl;
  // load token mgr
  ret = rgw_get_token_mgr_by_uid(store, uid, token_mgr);
  if(ret == 0) {
    bufferlist wbl;
    token_mgr.del_tid(tid);
    // reback
    encode(token_mgr, wbl);
    ret = rgw_store_token_mgr(store, uid, token_mgr);
  } else {
    ldout(store->ctx(), 0) << "ERROR: failed to read token mgr object [" << uid.to_str() << "]. Error: " << ret << dendl;
  }
  ldout(store->ctx(), 20) << "token mgr delete token id UNLOCK! " << tid << dendl;
  l.unlock();

  return ret;
}

int rgwinner::TokenLock::init(RGWRados *store, const rgw_user& u) {
  utime_t time(inner_token_lock_timeout, 0);
  l.set_duration(time);
  oid = u.to_str() + ".locked";
  return rgw_init_ioctx(store->get_rados_handle(), store->get_zone_params().user_token_pool, token_ctx, true);
}

int rgwinner::TokenLock::lock() {
  int ret; 
  do {
    ret = l.lock_exclusive(&token_ctx, oid);
  } while(ret != 0);
  return ret;
}

int rgwinner::TokenLock::unlock() {
  return l.unlock(&token_ctx, oid);
}

int32_t rgwinner::TokenMgr::count() {
  return int32_t(token_list.size());
}

int rgwinner::TokenMgr::add_tid(boost::string_view tid) {
  auto it = std::find(token_list.begin(), token_list.end(), tid.to_string());
  if(it != token_list.end())
    return -EEXIST;
  token_list.push_back(tid.to_string());
  return 0;
}

int rgwinner::TokenMgr::del_tid(boost::string_view tid) {
  auto it = std::find(token_list.begin(), token_list.end(), tid.to_string());
  if(it != token_list.end())
    token_list.erase(it);
  return 0;
}

int rgwinner::TokenMgr::init(RGWRados *store, rgw_user *u) {
  // Read TokenMgr object.
  this->store = store;
  this->user_id = u;
  int ret = rgw_get_token_mgr_by_uid(store, *u, *this);

  for(auto i = token_list.begin(); i != token_list.end(); i++) {
    ldout(store->ctx(), 20) << " TokenMgr init , token id : " << *i << dendl;
  }

  return ret;
}

int rgwinner::TokenMgr::add_token(boost::string_view tid, const Token& token) {
  int ret = 0;
  auto it = std::find(token_list.begin(), token_list.end(), tid.to_string());
  if (it != token_list.end())
    return -EEXIST;
  // persist tokens
  ret = rgw_store_token(store, tid, token);
  if(ret == 0) {
    // persist token mgr
    ret = rgw_token_mgr_add_tid(store, *user_id, tid);
  }
  return ret;
}

int rgwinner::TokenMgr::rm_token(boost::string_view tid) {
  int ret = 0;
  // remove token
  ret = rgw_remove_token(store, tid);
  auto it = std::find(token_list.begin(), token_list.end(), tid.to_string());
  if (it == token_list.end())
    return ret;
  if(ret == 0) {
    // persist token mgr
    ret = rgw_token_mgr_del_tid(store, *user_id, tid);
  }
  return ret;
}

int rgwinner::Token::init(RGWRados *store, std::string_view ak) {
  this->store = store;
  access_key = ak;
  return 0;
}

std::string rgwinner::Token::generate_key(std::string base64_token) {
  const auto hash = calc_hash_sha256(base64_token);
  return std::string(buf_to_hex(hash).data());
}

int rgwinner::Token::auth() {
  utime_t now = ceph_clock_now();
  if(expire + (long)(create) < now.sec())
    return -ETIME;
  return 0;
}

int rgwinner::TokenHelper::auth(boost::string_view t) {
  Token token_info;
  std::string oid = Token::generate_key(t.to_string());
  int ret = rgw_get_token(store, oid, token_info);
  if(ret == 0)
    ret = token_info.auth();
  return ret;
}

RGWGDWorker::Event_t RGWGDWorker::get_front_event() {
  if(events.empty())
    return EVT_NONE;
  event_lock.Lock();
  Event_t e = events.front();
  events.pop_front();
  event_lock.Unlock();
  return e;
}

void *RGWGDWorker::entry() {
  while(true) {
    auto e = get_front_event();
    if(e == EVT_PROCESS){
      gd->process();
    }
    else if(e == EVT_STOP){
      break;
    }
    else{
      // sleep
      wait_lock.Lock();
      cond.Wait(wait_lock);
      wait_lock.Unlock();
    }
  }
  return NULL;
}

void RGWGDWorker::send(Event_t e) {
  event_lock.Lock();
  events.push_back(e);
  event_lock.Unlock();

  Mutex::Locker l(wait_lock);
  cond.Signal();
}

void RGWGarbageDisposer::start() {
  // start thread
  worker.create(name.c_str());
  // start timer
  timer.init();
  Mutex::Locker l(timer_lock);
  set_timer();
}

void RGWGarbageDisposer::stop() {
  // stop timer
  Mutex::Locker l(timer_lock);
  timer.cancel_all_events();
  timer.shutdown();
  // stop thread
  worker.send(RGWGDWorker::EVT_STOP);
  worker.join();
}

void RGWGarbageDisposer::wakeup() {
  worker.send(RGWGDWorker::EVT_PROCESS);
}

int RGWGDToken::process() {
  void *handle;
  std::string marker;
  std::string metadata_key = "user";

  int ret = store->meta_mgr->list_keys_init(metadata_key, marker, &handle);
  if (ret < 0) {
    ldout(store->ctx(), 0) << "ERROR: can't get key: " << cpp_strerror(-ret) << dendl;
    return -ret;
  }
  uint64_t left = 1000;
  bool truncated;
  do {
    list<string> keys;
    ret = store->meta_mgr->list_keys_next(handle, left, keys, &truncated);
    if (ret < 0 && ret != -ENOENT) {
      ldout(store->ctx(), 0) << "ERROR: lists_keys_next(): " << cpp_strerror(-ret) << dendl;
      return -ret;
    } if (ret != -ENOENT) {
      for (list<string>::iterator iter = keys.begin(); iter != keys.end(); ++iter) {
        ldout(store->ctx(), 20) << "key: " <<  *iter << dendl;
        ret = purge_invalid_tokens(*iter);
      }
    }
  } while (truncated && left > 0);

  store->meta_mgr->list_keys_complete(handle);
  return 0;
}

int RGWGDToken::purge_invalid_tokens(boost::string_view u) {
  int ret;
  rgwinner::TokenMgr token_mgr;
  rgw_user user(u.to_string());

  ret = token_mgr.init(store, &user);
  if(!(ret == 0 || ret == -ENOENT)) {
    ldout(store->ctx(), 0) << "purge_invalid_tokens ERROR: failed to init token mgr : " << ret  << dendl;
    return ret;
  }

  rgwinner::TokenMgr::Iterator it(&token_mgr);
  boost::string_view tid;
  while(true){
    tid = it.get_next();
    if(tid == "")
      break;
    // load token
    rgwinner::Token token_info;
    ret = rgw_get_token(store, tid, token_info);
    if(ret == 0) {
      // check token invalid
      if(0 != token_info.auth()) {
        // remove token
        ldout(store->ctx(), 20) << "purge_invalid_tokens token : " << tid  << " removed." << dendl;
        token_mgr.rm_token(tid);
      }
    }
  }

  return ret;
}

