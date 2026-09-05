/*
* Copyright (c) 2026 Aadityansha Verma. All rights reserved.
* This file is licensed under the Business Source License 1.1.
* See the LICENSE file in the project root for full terms.
*/

#ifndef SERVER_H

#define SERVER_H

#include <stdint.h>
#include <sys/socket.h> 
#include <netinet/in.h>
#include <sys/types.h> 
#include <unistd.h>
#include<string.h>
#include <stdlib.h>
#include <stdio.h>
#include "parser.h"
#include "db.h"
#include "storage.h"
#include "search/kmeans.h"

#include "search/baseline-flat.h"

typedef struct server{

 int clientfd;
}server_data_t; 


typedef enum {
    INVALID_PARAMETER = 400,    // Bad JSON, missing fields, untrained index
    UNAUTHORIZED_ACCESS = 401,  // Wrong API key
    WRONG_REQUEST = 404,        // DB doesn't exist, route doesn't exist
    INTERNAL_ERROR = 500        // Server crashed, malloc failed
} Client_error;


int server_init(int port);

#endif
