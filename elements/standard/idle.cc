/*
 * idle.{cc,hh} -- element just sits there and kills packets
 * Robert Morris, Eddie Kohler
 *
 * Copyright (c) 1999-2000 Massachusetts Institute of Technology
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, subject to the following
 * conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * Further elaboration of this license, including a DISCLAIMER OF ANY
 * WARRANTY, EXPRESS OR IMPLIED, is provided in the LICENSE file, which is
 * also accessible at http://www.pdos.lcs.mit.edu/click/license.html
 */

#include <click/config.h>
#include <click/package.hh>
#include "idle.hh"
#include <click/bitvector.hh>
#include "scheduleinfo.hh"

Idle::Idle() 
{
  MOD_INC_USE_COUNT;
}

Idle::~Idle()
{
  MOD_DEC_USE_COUNT;
}

void
Idle::notify_ninputs(int n)
{
  set_ninputs(n);
}

void
Idle::notify_noutputs(int n)
{
  set_noutputs(n);
}

Bitvector
Idle::forward_flow(int) const
{
  return Bitvector(noutputs(), false);
}

Bitvector
Idle::backward_flow(int) const
{
  return Bitvector(ninputs(), false);
}

void
Idle::push(int, Packet *p)
{
  p->kill();
}

Packet *
Idle::pull(int)
{
  return 0;
}

EXPORT_ELEMENT(Idle)
ELEMENT_MT_SAFE(Idle)
