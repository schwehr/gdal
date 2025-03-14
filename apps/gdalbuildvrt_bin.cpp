/******************************************************************************
 *
 * Project:  GDAL Utilities
 * Purpose:  Command line application to build VRT datasets from raster products
 *or content of SHP tile index Author:   Even Rouault, <even dot rouault at
 *spatialys dot com>
 *
 ******************************************************************************
 * Copyright (c) 2007-2016, Even Rouault <even dot rouault at spatialys dot com>
 *
 * SPDX-License-Identifier: MIT
 ****************************************************************************/

#include "cpl_string.h"
#include "cpl_error.h"
#include "commonutils.h"
#include "gdal_version.h"
#include "gdal_utils_priv.h"

/************************************************************************/
/*                               Usage()                                */
/************************************************************************/

static void Usage() CPL_NO_RETURN;

static void Usage()

{
    fprintf(stderr, "%s\n", GDALBuildVRTGetParserUsage().c_str());
    exit(1);
}

/************************************************************************/
/*                                main()                                */
/************************************************************************/

MAIN_START(argc, argv)

{
    EarlySetConfigOptions(argc, argv);

    /* -------------------------------------------------------------------- */
    /*      Register standard GDAL drivers, and process generic GDAL        */
    /*      command options.                                                */
    /* -------------------------------------------------------------------- */
    GDALAllRegister();
    argc = GDALGeneralCmdLineProcessor(argc, &argv, 0);
    if (argc < 1)
        exit(-argc);

    GDALBuildVRTOptionsForBinary sOptionsForBinary;
    /* coverity[tainted_data] */
    GDALBuildVRTOptions *psOptions =
        GDALBuildVRTOptionsNew(argv + 1, &sOptionsForBinary);
    CSLDestroy(argv);

    if (psOptions == nullptr)
    {
        Usage();
    }

    if (!(sOptionsForBinary.bQuiet))
    {
        GDALBuildVRTOptionsSetProgress(psOptions, GDALTermProgress, nullptr);
    }

    /* Avoid overwriting a non VRT dataset if the user did not put the */
    /* filenames in the right order */
    VSIStatBuf sBuf;
    if (!sOptionsForBinary.bOverwrite)
    {
        int bExists =
            (VSIStat(sOptionsForBinary.osDstFilename.c_str(), &sBuf) == 0);
        if (bExists)
        {
            GDALDriverH hDriver = GDALIdentifyDriver(
                sOptionsForBinary.osDstFilename.c_str(), nullptr);
            if (hDriver &&
                !(EQUAL(GDALGetDriverShortName(hDriver), "VRT") ||
                  (EQUAL(GDALGetDriverShortName(hDriver), "API_PROXY") &&
                   EQUAL(CPLGetExtensionSafe(
                             sOptionsForBinary.osDstFilename.c_str())
                             .c_str(),
                         "VRT"))))
            {
                fprintf(
                    stderr,
                    "'%s' is an existing GDAL dataset managed by %s driver.\n"
                    "There is an high chance you did not put filenames in the "
                    "right order.\n"
                    "If you want to overwrite %s, add -overwrite option to the "
                    "command line.\n\n",
                    sOptionsForBinary.osDstFilename.c_str(),
                    GDALGetDriverShortName(hDriver),
                    sOptionsForBinary.osDstFilename.c_str());
                Usage();
            }
        }
    }

    int bUsageError = FALSE;
    GDALDatasetH hOutDS = GDALBuildVRT(
        sOptionsForBinary.osDstFilename.c_str(),
        sOptionsForBinary.aosSrcFiles.size(), nullptr,
        sOptionsForBinary.aosSrcFiles.List(), psOptions, &bUsageError);
    if (bUsageError)
        Usage();
    int nRetCode = (hOutDS) ? 0 : 1;

    GDALBuildVRTOptionsFree(psOptions);

    CPLErrorReset();
    // The flush to disk is only done at that stage, so check if any error has
    // happened
    if (GDALClose(hOutDS) != CE_None)
        nRetCode = 1;
    if (CPLGetLastErrorType() != CE_None)
        nRetCode = 1;

    GDALDumpOpenDatasets(stderr);

    GDALDestroyDriverManager();

    OGRCleanupAll();

    return nRetCode;
}

MAIN_END
