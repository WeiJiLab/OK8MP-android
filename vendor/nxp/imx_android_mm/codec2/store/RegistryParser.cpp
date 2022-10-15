/**
 *  Copyright 2020 NXP
 *  All Rights Reserved.
 *
 *  The following programs are the sole property of NXP,
 *  and contain its proprietary and confidential information.
 */

//#define LOG_NDEBUG 0
#define LOG_TAG "RegistryParser"
#include <log/log.h>
#include <utils/Log.h>
#include <string.h>
#include "RegistryParser.h"

RegistryParser::RegistryParser()
{
    registerdCompNum = 0;
    includeFilesNum = 0;
    memset(RegList, 0, sizeof(REG_ENTRY)*ITEM_NUM);
    memset(includeFiles, 0, sizeof(includeFiles));
    ALOGV("RegistryParser");
}

RegistryParser::~RegistryParser()
{
}


int RegistryParser::GetRegCompNum()
{
    return registerdCompNum;
}

REG_ENTRY * RegistryParser::QueryRegComp(int index)
{
    if (index >= registerdCompNum)
        return NULL;

    return &RegList[index];
}

REG_ERRORTYPE RegistryParser::ParseRegList(char *file_name) {

    if (file_name == NULL)
	{
		ALOGE("Input file name is NULL.");
		return REG_FAILURE;
	}

    DoParseRegList(file_name);

    int i;
    for (i=0; i< includeFilesNum; i++) {
        DoParseRegList(includeFiles[i]);
    }

    return REG_SUCCESS;
}

REG_ERRORTYPE RegistryParser::DoParseRegList(char *file_name)
{
	char symbol;
	bool bSkip = false;
	char symbolBuffer[ITEM_NAME_LEN] = {0}, *pStrSeg;
	bool EntryFounded = false;
	int symbolCnt = 0;
	REG_ENTRY *pRegEntry;
    FILE *pfile = NULL;
    int UseOffset = 0,  BufferDataLen = 0;
    bool FileReadEnd = false;
    bool getCompName = false, getLibName = false;
    char readBuffer[FILE_READ_SIZE];

    pfile = fopen(file_name, "r");
	if (pfile == NULL)
	{
		ALOGE("open file %s failed.\n", file_name);
		return REG_FAILURE;
	}

    memset(readBuffer, 0, FILE_READ_SIZE);

	while (BufferDataLen - UseOffset > 0 || FileReadEnd == false)
	{
	    if (BufferDataLen - UseOffset == 0)
		{
			BufferDataLen = fread(readBuffer, 1, FILE_READ_SIZE, pfile);
			if (0 == BufferDataLen)
			{
			    break;
			}

			if (BufferDataLen < FILE_READ_SIZE)
			{
				FileReadEnd = true;
			}

			UseOffset = 0;
		}

		symbol = readBuffer[UseOffset];
		UseOffset ++;
        //ALOGD(" symbol %c", symbol);

		if (bSkip == true)
		{
			if (symbol == '\n')
			{
				bSkip = false;
			}
		}
		else
		{
			if (symbol == '#')
			{
				bSkip = true;
			}
			else if (symbol == '\t'
					|| symbol == ' '
					|| symbol == '\n'
					|| symbol == '\r')
			{
			}
			else if (symbol == '@')
			{
				EntryFounded = true;
			}
			else if (symbol == '$')
			{
			    if (EntryFounded && getCompName && getLibName) {
                    ALOGV("register comp index %d %s %s ", registerdCompNum, pRegEntry->compName, pRegEntry->libName);
                    registerdCompNum++;
                }
                EntryFounded = false;
                getCompName = false;
                getLibName = false;
				continue;
			}
			else if (symbol == ';')
			{
				char *pLast = NULL;
				if (symbolCnt != 0 && ((pStrSeg = strtok_r(symbolBuffer, "=", &pLast)) != NULL))
				{
				    //ALOGD("pStrSeg=%s symbolBuffer %s", pStrSeg, symbolBuffer);
                    pRegEntry = &RegList[registerdCompNum];
                    if (!strncmp(pStrSeg, "component_name", strlen("component_name"))) {
                        pStrSeg = strtok_r(NULL, "=", &pLast);
                        if (pStrSeg && strlen(pStrSeg) < ITEM_NAME_LEN) {
                            strcpy(pRegEntry->compName, pStrSeg);
                            getCompName = true;
                        }
                        ALOGV("registerdCompNum %d compName=%s", registerdCompNum, pRegEntry->compName);
                    } else if (!strncmp(pStrSeg, "library_path", strlen("library_path"))) {
                        pStrSeg = strtok_r(NULL, "=", &pLast);
                        if (pStrSeg && strlen(pStrSeg) < ITEM_NAME_LEN) {
                            strcpy(pRegEntry->libName, pStrSeg);
                            getLibName = true;
                        }
                        ALOGV("registerdCompNum %d libName=%s", registerdCompNum, pRegEntry->libName);
                    } else if (!strncmp(pStrSeg, "include_file", strlen("include_file"))) {
                        pStrSeg = strtok_r(NULL, "=", &pLast);
                        if (pStrSeg && strlen(pStrSeg) < ITEM_NAME_LEN) {
                            strcpy(includeFiles[includeFilesNum], pStrSeg);
                            ALOGV("include_file=%s", pStrSeg);
                            includeFilesNum++;
                        }
                    }
					memset(symbolBuffer, 0, ITEM_NAME_LEN);
					symbolCnt = 0;
				}
			}
			else
			{
				if (EntryFounded == true)
				{
					symbolBuffer[symbolCnt] = symbol;
					symbolCnt ++;
				}
			}
		}
	}

    if (pfile) {
        fclose(pfile);
        pfile = NULL;
    }
	return REG_SUCCESS;
}

/* File EOF */
