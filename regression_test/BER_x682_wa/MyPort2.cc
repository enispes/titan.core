///////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2000-2014 Ericsson Telecom AB
// All rights reserved. This program and the accompanying materials
// are made available under the terms of the Eclipse Public License v1.0
// which accompanies this distribution, and is available at
// http://www.eclipse.org/legal/epl-v10.html
///////////////////////////////////////////////////////////////////////////////
// This Test Port skeleton source file was generated by the
// TTCN-3 Compiler of the TTCN-3 Test Executor version 1.3.pl0
// for Matyas Forstner (tmpmfr@saussure) on Thu Apr 10 16:06:07 2003
//
// You may modify this file. Complete the body of empty functions and
// add your member functions here.

#include "MyPort2.hh"

MyPort2::MyPort2(const char *par_port_name)
	: MyPort2_BASE(par_port_name)
{

}

MyPort2::~MyPort2()
{

}

void MyPort2::set_parameter(const char *parameter_name,
	const char *parameter_value)
{

}

void MyPort2::Event_Handler(const fd_set *read_fds,
	const fd_set *write_fds, const fd_set *error_fds,
	double time_since_last_call)
{

}

void MyPort2::user_map(const char *system_port)
{

}

void MyPort2::user_unmap(const char *system_port)
{

}

void MyPort2::user_start()
{

}

void MyPort2::user_stop()
{

}

void MyPort2::outgoing_send(const OCTETSTRING& send_par)
{
  BER_Buf BER_buf;
  BER_buf.put_s(send_par.lengthof(), send_par);

  ErrorReturn pdu;
  pdu.decode_BER(BER_buf, ErrorReturn_tag, BER_ACCEPT_ALL);
  incoming_message(pdu);
}

