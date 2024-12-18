
/******************************************************************************
* Project:  GDAL/OGR Java bindings
* Purpose:  Add javadoc located in a special file into generated SWIG Java files
* Author:   Even Rouault <even dot rouault at spatialys.com>
*
*******************************************************************************
 * Copyright (c) 2009, Even Rouault <even dot rouault at spatialys.com>
*
 * SPDX-License-Identifier: MIT
*******************************************************************************/

/* NOTE : this is really a quick and very dirty hack to put the javadoc contained */
/* in a special formatted file, javadoc.java, into the SWIG generated java files */
/* This program leaks memory and would crash easily on unexpected inputs */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TRUE 1
#define FALSE 0

typedef struct
{
    char* methodName;
    char* compactMethodName;
    char* javadoc;
    int bUsed;
    int bHide;
} JavaDocInstance;

char* stripline(char* pszBuf)
{
    int i = 0;
    while(pszBuf[i] == ' ' && pszBuf[i] != 0)
        i ++;
    memmove(pszBuf, pszBuf + i, strlen(pszBuf) - i + 1);

    i = strlen(pszBuf) - 1;
    while(i > 0 && (pszBuf[i] == '{' || pszBuf[i] == '\n' || pszBuf[i] == ' '))
    {
        pszBuf[i] = 0;
        i--;
    }

    return pszBuf;
}

/* Remove argument names from function prototype, so that we used the */
/* argument names from javadoc.java instead of the ones provided by default by the */
/* bindings */
char* removeargnames(char* pszBuf)
{
    if (strstr(pszBuf, "="))
    {
        *strstr(pszBuf, "=") = 0;
        stripline(pszBuf);
    }

    if (strstr(pszBuf, "(") == NULL)
        return pszBuf;

    //fprintf(stderr, "%s\n", pszBuf);

    if (strstr(pszBuf, "{"))
    {
        *strstr(pszBuf, "{") = 0;
        stripline(pszBuf);
    }
    else
    {
        int i = strstr(pszBuf, ")") - pszBuf - 1;
        char* lastOK = strstr(pszBuf, ")");
        while(i>0 && pszBuf[i] == ' ')
            i --;
        while(i>0)
        {
            if (pszBuf[i] == '(')
                break;
            if (pszBuf[i] == ' ' || pszBuf[i] == ']')
            {
                if (pszBuf[i] == ' ')
                    memmove(pszBuf + i + 1, lastOK, strlen(lastOK) + 1);
                else
                    memmove(pszBuf + i, lastOK, strlen(lastOK) + 1);
                while(pszBuf[i] != ',' && pszBuf[i] != '(')
                    i --;
                if (pszBuf[i] == ',')
                    lastOK = pszBuf + i;
                else
                    break;
            }
            i --;
        }
        i = strstr(pszBuf, "(") - pszBuf;
        while(pszBuf[i])
        {
            if (pszBuf[i] == ' ')
                memmove(pszBuf + i, pszBuf + i + 1, strlen(pszBuf + i + 1) + 1);
            else
                i++;
        }
    }
    //fprintf(stderr, "after : %s\n", pszBuf);
    return pszBuf;
}

static void ignore_ret(char* x)
{
    (void)x;
}

int main(int argc, char* argv[])
{
    const char* patch_filename = argv[1];

    FILE* fSrc = fopen(patch_filename, "rt");
    if( fSrc == NULL )
    {
        fprintf(stderr, "Cannot open %s\n", patch_filename);
        return 1;
    }
    FILE* fDst;
    JavaDocInstance* instances = (JavaDocInstance*)calloc(sizeof(JavaDocInstance), 3000);
    int nInstances = 0;
    char szLine[1024];
    char szClass[256];
    char javadoc[16384];
    szClass[0] = 0;
    while(fgets(szLine, 1023, fSrc))
    {
        if (strstr(szLine, "/**") == NULL) continue;
begin:
        strcpy(javadoc, szLine);
        while(fgets(szLine, 1023, fSrc))
        {
            strcat(javadoc, szLine);
            if (strstr(szLine, "*/"))
                break;
        }
        while(fgets(szLine, 1023, fSrc))
        {
            if (szLine[0] == 10)
                continue;
            else if (strstr(szLine, "*") == NULL)
            {
                instances[nInstances].javadoc = strdup(javadoc);

                char* pszLine = szLine;
                if (strncmp(pszLine, "@hide ", 6) == 0)
                {
                    instances[nInstances].bHide = 1;
                    pszLine += 6;
                }
                else
                    instances[nInstances].bHide = 0;

                instances[nInstances].methodName = strdup(stripline(pszLine));
                instances[nInstances].compactMethodName = strdup(removeargnames(stripline(pszLine)));
                nInstances++;
            }
            else
                break;
        }
        if (strstr(szLine, "/**") != NULL)
            goto begin;
    }
    //fprintf(stderr, "nInstances=%d\n", nInstances);
    fclose(fSrc);

    int i;
    for(i=3;i<argc;i++)
    {
        if( strstr(argv[i], "AsyncReader.java") )
        {
            fprintf(stderr, "Skipping %s\n", argv[i]);
            continue;
        }
        fSrc = fopen(argv[i], "rt");
        if (fSrc == NULL)
        {
            fprintf(stderr, "Cannot open %s\n", argv[i]);
            continue;
        }
        char szDstName[1024];
        sprintf(szDstName, "%s/%s", argv[2], argv[i]);
        fDst = fopen(szDstName, "wt");
        if (fDst == NULL)
        {
            fprintf(stderr, "Cannot write %s\n", szDstName);
            continue;
        }
        szClass[0] = 0;
        char szPackage[256];
        szPackage[0] = 0;

        while(fgets(szLine, 1023, fSrc))
        {
            char szMethodName[2048];
            char* szOriLine = strdup(szLine);
            if (strstr(szLine, "package"))
            {
                strcpy(szPackage, szLine);
            }
            else if (strstr(szLine, "public class") || strstr(szLine, "public interface"))
            {
                strcpy(szClass, stripline(szLine));
                if (strstr(szClass, "extends"))
                {
                    *strstr(szClass, "extends") = 0;
                    stripline(szClass);
                }
                if (strstr(szClass, "implements"))
                {
                    *strstr(szClass, "implements") = 0;
                    stripline(szClass);
                }
                if (strstr(szLine, "Driver"))
                {
                    if (strstr(szPackage, "org.gdal.gdal"))
                        strcpy(szLine, "public class org.gdal.gdal.Driver");
                    else
                        strcpy(szLine, "public class org.gdal.ogr.Driver");
                    strcpy(szClass, szLine);
                }
            }
            if (strstr(szLine, "synchronized "))
            {
                char* c = strstr(szLine, "synchronized ");
                *c = 0;
                memmove(c, c + 13, strlen(c + 13) + 1);
            }
            if (strstr(szLine, "public") && !strstr(szLine, "native"))
            {
                if (strchr(szLine, '(') && !strchr(szLine,')'))
                {
                    strcpy(szMethodName, szLine);
                    do
                    {
                        ignore_ret(fgets(szLine, 1023, fSrc));
                        strcpy(szMethodName + strlen(szMethodName) - 1, szLine);
                    } while (!strchr(szMethodName,')'));
                    strcpy(szLine, szMethodName);
                    free(szOriLine);
                    szOriLine = strdup(szMethodName);
                    //fprintf(stderr, "%s\n", szOriLine);
                }
                if (strchr(szLine, '(') || strchr(szLine, '='))
                    sprintf(szMethodName, "%s:%s", szClass, removeargnames(stripline(szLine)));
                else
                    strcpy(szMethodName, szClass);
                //fprintf(stderr, "%s\n", szMethodName);
                int j;
                for(j=0;j<nInstances;j++)
                {
                    if (strcmp(instances[j].compactMethodName, szMethodName) == 0)
                    {
                        instances[j].bUsed = TRUE;

                        //fprintf(stderr, "found match for %s\n", szMethodName);
                        if (instances[j].bHide)
                        {
                            if (strstr(szLine, "final static") == NULL)
                            {
                                do
                                {
                                    ignore_ret(fgets(szLine, 1023, fSrc));
                                } while (!strchr(szLine,'}'));
                            }
                            break;
                        }

                        fprintf(fDst, "%s", instances[j].javadoc);
                        if (strchr(szMethodName, '('))
                        {
                            fprintf(fDst, "%s;\n", strchr(instances[j].methodName, ':')+1);
                            int nBrackets = 0;
                            int bFoundOpen = FALSE;
                            strcpy(szLine, szOriLine);
                            do
                            {
                                int j;
                                for(j=0;szLine[j];j++)
                                {
                                    if (szLine[j] == '{')
                                    {
                                        bFoundOpen = TRUE;
                                        nBrackets ++;
                                    }
                                    else if (szLine[j] == '}')
                                    {
                                        nBrackets --;
                                    }
                                }
                                ignore_ret(fgets(szLine, 1023, fSrc));
                            } while(bFoundOpen == FALSE || nBrackets > 0);
                        }
                        else
                            fprintf(fDst, "%s", szOriLine);
                        break;
                    }
                }
                if (j == nInstances)
                {
                    if (strstr(szOriLine, "public") && (strstr(szOriLine, "getCPtr") || strstr(szOriLine, "long cPtr")))
                    {
                        char* c = strstr(szOriLine, "public");
                        *c = 0;
                        fprintf(fDst, "%s private %s", szOriLine, c + 6);
                    }
                    else
                        fprintf(fDst, "%s", szOriLine);
                }
            }
            else
                fprintf(fDst, "%s", szOriLine);
            free(szOriLine);
        }

        fclose(fSrc);
        fclose(fDst);
    }

    int j;
    for(j=0;j<nInstances;j++)
    {
        if (!instances[j].bUsed)
            fprintf(stderr, "WARNING: did not find occurrence of %s\n", instances[j].methodName);
    }

    return 0;
}
