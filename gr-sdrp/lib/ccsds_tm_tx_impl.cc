/* -*- c++ -*- */
/*
 * Copyright 2005,2010,2013 Free Software Foundation, Inc.
 *
 * This file is part of GNU Radio
 *
 * GNU Radio is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3, or (at your option)
 * any later version.
 *
 * GNU Radio is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with GNU Radio; see the file COPYING.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street,
 * Boston, MA 02110-1301, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "ccsds_tm_tx_impl.h"
#include "fec/viterbi27_port.h"
#include "fec/encode_rs_ccsds.h"
#include "fec/fixed.h"
#include <gnuradio/io_signature.h>
#include <cstdio>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdexcept>
#include <string.h>

namespace gr {
namespace sdrp {

ccsds_tm_tx::sptr ccsds_tm_tx::make(){
	return gnuradio::get_initial_sptr(new ccsds_tm_tx_impl());
}

ccsds_tm_tx_impl::ccsds_tm_tx_impl()
	: sync_block("ccsds_tm_tx",
		io_signature::make(0, 0, 0),
		io_signature::make(1, 1, 1)){

	//Incoming messages consist of arrays of uint8_t's corresponding to the desired data bytes
	message_port_register_in(pmt::mp("ccsds_tx_msg_in"));
	set_msg_handler(pmt::mp("ccsds_tx_msg_in"), boost::bind(&ccsds_tm_tx_impl::inTXMsg, this, _1));

	//Default to uncoded modulation with frame length of 8920
	d_coding_method = METHOD_NONE;
	d_frame_len = 8920;
}

ccsds_tm_tx_impl::~ccsds_tm_tx_impl(){
}

void ccsds_tm_tx_impl::inTXMsg(pmt::pmt_t msg){
	//This should be purely a message containing uint8's
	pmt::pmt_t msg_vector = pmt::cdr(msg);
	size_t msg_len = pmt::length(msg_vector);
	size_t offset(0);
	uint8_t *msg_array = (uint8_t*)(pmt::uniform_vector_elements(msg_vector, offset));

	//Make a vector and push it on to the queue of vectors to push out
	std::vector<uint8_t> new_packet;
	new_packet.assign(msg_array, msg_array+msg_len);
	d_packet_queue.push(new_packet);
}

void ccsds_tm_tx_impl::setCodeRate(unsigned int r_mult, unsigned int r_div){
	d_r_mult = r_mult;
	d_r_div = r_div;
	//TODO: Should this modify d_fram_len as well???
}

void ccsds_tm_tx_impl::setFrameLength(int in_frame_len){
	d_frame_len = in_frame_len;
}

void ccsds_tm_tx_impl::setCodingMethod(std::string in_method){
	if(in_method == "CONV")
		d_coding_method = METHOD_CONV;
	else if(in_method == "RS"){
		d_coding_method = METHOD_RS;
		d_frame_len = 223*8;
		d_r_mult = 255;
		d_r_div = 223;
	}else if(in_method == "CC")
		d_coding_method = METHOD_CC;
	else if(in_method == "Turbo")
		d_coding_method = METHOD_TURBO;
	else if(in_method == "LDPC")
		d_coding_method = METHOD_LDPC;
	else
		d_coding_method = METHOD_NONE;
}

void ccsds_tm_tx_impl::setCodingParameter(std::string param_name, std::string param_val){
	if(d_coding_method == METHOD_RS){
		if(param_name == "E")
			d_rs_e = (unsigned int)atoi(param_val.c_str());
		else if(param_name == "I")
			d_rs_i = (unsigned int)atoi(param_val.c_str());
		else if(param_name == "Q")
			d_rs_q = (unsigned int)atoi(param_val.c_str());
	} else if(d_coding_method == METHOD_TURBO){
		if(param_name == "K")
			d_turbo_k = (unsigned int)atoi(param_val.c_str());
	} else if(d_coding_method == METHOD_LDPC){
		if(param_name == "K")
			d_ldpc_k = (unsigned int)atoi(param_val.c_str());
	}
}

void ccsds_tm_tx_impl::setAccessCode(std::string access_code){

	//Convert everything into uint8_t and store into allocated vector
	d_access_code.clear();
	for(int ii=0; ii < access_code.size(); ii++)
		d_access_code.push_back((uint8_t)access_code[ii]);
}

void ccsds_tm_tx_impl::pushByte(uint8_t in_byte){
	//Assuming MSB-first
	for(int ii=0; ii < 8; ii++){
		uint8_t cur_bit = (in_byte & (1 << (7-ii))) ? 1 : 0;
		d_historic_bits.push(cur_bit);
	}
}

std::vector<uint8_t> ccsds_tm_tx_impl::unpackBits(std::vector<uint8_t> in_packet){
	std::vector<uint8_t> return_bits;
	for(unsigned int ii=0; ii < in_packet.size(); ii++){
		for(int jj=0; jj < 8; jj++){
			uint8_t cur_bit = (in_packet[ii] & (1 << (7-ii))) ? 1 : 0;
			return_bits.push_back(cur_bit);
		}
	}
}

int ccsds_tm_tx_impl::work(int noutput_items,
			gr_vector_const_void_star &input_items,
			gr_vector_void_star &output_items){
	char *out = (char*)output_items[0];
	int nn = 0;

	while(nn < noutput_items) {
		if(d_historic_bits.size() > 0){
			//Push out any bits that are waiting to go out...
			while(nn < noutput_items && d_historic_bits.size() > 0){
				out[nn++] = d_historic_bits.front();
				d_historic_bits.pop();
			}
		} else if(d_packet_queue.size() > 0){
			std::vector<uint8_t> cur_packet = d_packet_queue.front();
			d_packet_queue.pop();

			//Put in logic here to unpack bits and encode in TM format......
			for(unsigned int ii=0; ii < d_access_code.size(); ii++)
				pushByte(d_access_code[ii]);

			//Encoding varies depending on the coding method used...
			if(d_coding_method == METHOD_NONE){
				//Nothing else needs to be done in the no-encoding case
				for(unsigned int ii=0; ii < cur_packet.size(); ii++)
					pushByte(cur_packet[ii]);
			} else if(d_coding_method == METHOD_CONV){
				//Encode using r=1/2 k=7 CCSDS code
				std::vector<uint8_t> payload_bits = unpackBits(cur_packet);
				std::vector<uint8_t> encoded_bits(cur_packet.size()*2+6);
				encode_viterbi27_port(&payload_bits[0], payload_bits.size(), &encoded_bits[0]);
				for(unsigned int ii=0; ii < encoded_bits.size(); ii++)
					d_historic_bits.push(encoded_bits[ii]);
				
			} else if(d_coding_method == METHOD_RS){
				//Get the appropriate parity symbols for the data (E=16)
				std::vector<uint8_t> cur_packet_parity(NROOTS);
				encode_rs_ccsds(&cur_packet[0], &cur_packet_parity[0], 0);
				for(unsigned int ii=0; ii < cur_packet.size(); ii++)
					pushByte(cur_packet[ii]);
				for(unsigned int ii=0; ii < NROOTS; ii++)
					pushByte(cur_packet_parity[ii]);
			} else {
				//TODO: Implement the remaining coding methods...
			}
		} else {
			return 0;
		}
	}

	return nn;
}

} /* namespace sdrp */
} /* namespace gr */
