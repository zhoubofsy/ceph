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

#ifndef RGW_INNER_TOKEN_H
#define RGW_INNER_TOKEN_H

#include "common/Timer.h"
#include "common/Cond.h"
#include "common/Thread.h"
#include "cls/lock/cls_lock_client.h"
#include "common/Mutex.h"

namespace rgwinner{
  class TokenLock {
    private:
      librados::IoCtx token_ctx;
      rados::cls::lock::Lock l;
      std::string oid;
    public:
      TokenLock():l("token_mgr_add") {}
      ~TokenLock() = default;
      int init(RGWRados *store, const rgw_user& u);
      int lock();
      int unlock();
  };
}

class RGWGarbageDisposer;


class RGWGDWorker : public Thread {
public:
  enum Event_t : int {
    EVT_NONE,
    EVT_STOP,
    EVT_PROCESS
  };

  RGWGDWorker(RGWGarbageDisposer* d):gd(d),wait_lock("GDWorker"),event_lock("GDWorker_Events") {}
  ~RGWGDWorker() = default;

  void *entry() override;
  void send(Event_t e);
private:
  RGWGarbageDisposer *gd;
  Mutex wait_lock;
  Cond cond;

  std::list<Event_t> events;
  Mutex event_lock;

  Event_t get_front_event();
};

class RGWGarbageDisposer {
  RGWGDWorker worker;
  int       interval;
  Mutex     timer_lock;
  SafeTimer timer;
  const string name;

  class GDTimerContext : public Context {
    RGWGarbageDisposer *gd;
    public:
    explicit GDTimerContext(RGWGarbageDisposer* d) : gd(d) {}
    void finish(int r) override {
      gd->wakeup();
      gd->set_timer();
    }
  };

  void set_timer() {
    timer.add_event_after(interval, new GDTimerContext(this));
  }
public:
  virtual int process() = 0;
  RGWGarbageDisposer(CephContext *cct, boost::string_view n, int i):worker(this),interval(i),timer_lock("GDTimer"),timer(cct, timer_lock),name(n.to_string()) {}
  virtual ~RGWGarbageDisposer() {}
  void start();
  void stop();
  void wakeup();
};

class RGWGDToken : public RGWGarbageDisposer {
  CephContext *cct;
  RGWRados *store;
public:
  RGWGDToken(CephContext* cct, RGWRados* store):RGWGarbageDisposer(cct, "RGWGDToken", cct->_conf->rgw_gd_token_processor_period),cct(cct),store(store) {}
  ~RGWGDToken() {
    this->stop();
  }
  int process() override;
  int purge_invalid_tokens(boost::string_view u);
};

#endif
