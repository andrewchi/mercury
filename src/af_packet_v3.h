/*
 * af_packet_v3.h
 *
 * Copyright (c) 2019 Cisco Systems, Inc. All rights reserved.
 * License at https://github.com/cisco/mercury/blob/master/LICENSE
 */

#ifndef AF_PACKET_V3
#define AF_PACKET_V3

#include "mercury.h"
#include "output.h"

int af_packet_bind_and_dispatch(struct mercury_config *cfg,
                                struct output_file *out_ctx);

#endif /* AF_PACKET_V3 */
