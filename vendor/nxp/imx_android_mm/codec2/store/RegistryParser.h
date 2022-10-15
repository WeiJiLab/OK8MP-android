/**
 *  Copyright 2020 NXP
 *  All Rights Reserved.
 *
 *  The following programs are the sole property of NXP,
 *  and contain its proprietary and confidential information.
 */

/**
 *  @file RegistryParser.h
 *  @brief Class definition of Regitsry file analyser
 *  @ingroup RegistryParser
 */


#ifndef RegistryParser_h
#define RegistryParser_h

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

#define ITEM_NUM 32
#define ITEM_NAME_LEN 128
#define FILE_READ_SIZE (4096)

typedef enum {
    REG_SUCCESS,
    REG_FAILURE,
    REG_INSUFFICIENT_RESOURCES,
    REG_INVALID_REG
}REG_ERRORTYPE;

typedef struct _REG_ENTRY {
    char compName[ITEM_NAME_LEN];
    char libName[ITEM_NAME_LEN];
}REG_ENTRY;

class RegistryParser {
	public:
		RegistryParser();
		REG_ERRORTYPE ParseRegList(char *file_name);
        int GetRegCompNum();
        REG_ENTRY * QueryRegComp(int index);
		~RegistryParser();
	private:
		REG_ENTRY RegList[ITEM_NUM];
        int registerdCompNum;
        int includeFilesNum;
        char includeFiles[ITEM_NUM][ITEM_NAME_LEN];

        REG_ERRORTYPE DoParseRegList(char *file_name);

};

#endif
/* File EOF */
