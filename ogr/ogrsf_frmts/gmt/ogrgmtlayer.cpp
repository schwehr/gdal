/******************************************************************************
 *
 * Project:  OpenGIS Simple Features Reference Implementation
 * Purpose:  Implements OGRGmtLayer class.
 * Author:   Frank Warmerdam, warmerdam@pobox.com
 *
 ******************************************************************************
 * Copyright (c) 2007, Frank Warmerdam <warmerdam@pobox.com>
 *
 * SPDX-License-Identifier: MIT
 ****************************************************************************/

#include "ogr_gmt.h"
#include "cpl_conv.h"
#include "ogr_p.h"

#include <algorithm>

/************************************************************************/
/*                            OGRGmtLayer()                             */
/************************************************************************/

OGRGmtLayer::OGRGmtLayer(GDALDataset *poDS, const char *pszFilename,
                         VSILFILE *fp, const OGRSpatialReference *poSRS,
                         int bUpdateIn)
    : m_poDS(poDS), poFeatureDefn(nullptr), iNextFID(0),
      bUpdate(CPL_TO_BOOL(bUpdateIn)),
      // Assume header complete in readonly mode.
      bHeaderComplete(CPL_TO_BOOL(!bUpdate)), bRegionComplete(false),
      nRegionOffset(0),
      m_fp(fp ? fp : VSIFOpenL(pszFilename, (bUpdateIn ? "r+" : "r"))),
      papszKeyedValues(nullptr), bValidFile(false)
{
    if (m_fp == nullptr)
        return;

    /* -------------------------------------------------------------------- */
    /*      Create the feature definition                                   */
    /* -------------------------------------------------------------------- */
    poFeatureDefn = new OGRFeatureDefn(CPLGetBasenameSafe(pszFilename).c_str());
    SetDescription(poFeatureDefn->GetName());
    poFeatureDefn->Reference();

    /* -------------------------------------------------------------------- */
    /*      Read the header.                                                */
    /* -------------------------------------------------------------------- */
    if (!STARTS_WITH(pszFilename, "/vsistdout"))
    {
        CPLString osFieldNames;
        CPLString osFieldTypes;
        CPLString osGeometryType;
        CPLString osRegion;
        CPLString osWKT;
        CPLString osProj4;
        CPLString osEPSG;
        vsi_l_offset nStartOfLine = 0;

        VSIFSeekL(m_fp, 0, SEEK_SET);

        while (ReadLine() && osLine[0] == '#')
        {
            if (strstr(osLine, "FEATURE_DATA"))
            {
                bHeaderComplete = true;
                ReadLine();
                break;
            }

            if (STARTS_WITH_CI(osLine, "# REGION_STUB "))
                nRegionOffset = nStartOfLine;

            for (int iKey = 0; papszKeyedValues != nullptr &&
                               papszKeyedValues[iKey] != nullptr;
                 iKey++)
            {
                if (papszKeyedValues[iKey][0] == 'N')
                    osFieldNames = papszKeyedValues[iKey] + 1;
                if (papszKeyedValues[iKey][0] == 'T')
                    osFieldTypes = papszKeyedValues[iKey] + 1;
                if (papszKeyedValues[iKey][0] == 'G')
                    osGeometryType = papszKeyedValues[iKey] + 1;
                if (papszKeyedValues[iKey][0] == 'R')
                    osRegion = papszKeyedValues[iKey] + 1;
                if (papszKeyedValues[iKey][0] == 'J' &&
                    papszKeyedValues[iKey][1] != 0 &&
                    papszKeyedValues[iKey][2] != 0)
                {
                    std::string osArg = papszKeyedValues[iKey] + 2;
                    if (osArg[0] == '"' && osArg.size() >= 2 &&
                        osArg.back() == '"')
                    {
                        osArg = osArg.substr(1, osArg.length() - 2);
                        char *pszArg = CPLUnescapeString(
                            osArg.c_str(), nullptr, CPLES_BackslashQuotable);
                        osArg = pszArg;
                        CPLFree(pszArg);
                    }

                    if (papszKeyedValues[iKey][1] == 'e')
                        osEPSG = std::move(osArg);
                    if (papszKeyedValues[iKey][1] == 'p')
                        osProj4 = std::move(osArg);
                    if (papszKeyedValues[iKey][1] == 'w')
                        osWKT = std::move(osArg);
                }
            }

            nStartOfLine = VSIFTellL(m_fp);
        }

        /* --------------------------------------------------------------------
         */
        /*      Handle coordinate system. */
        /* --------------------------------------------------------------------
         */
        if (osWKT.length())
        {
            m_poSRS = new OGRSpatialReference();
            m_poSRS->SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);
            if (m_poSRS->importFromWkt(osWKT.c_str()) != OGRERR_NONE)
            {
                delete m_poSRS;
                m_poSRS = nullptr;
            }
        }
        else if (osEPSG.length())
        {
            m_poSRS = new OGRSpatialReference();
            m_poSRS->SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);
            if (m_poSRS->importFromEPSG(atoi(osEPSG)) != OGRERR_NONE)
            {
                delete m_poSRS;
                m_poSRS = nullptr;
            }
        }
        else if (osProj4.length())
        {
            m_poSRS = new OGRSpatialReference();
            m_poSRS->SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);
            if (m_poSRS->importFromProj4(osProj4) != OGRERR_NONE)
            {
                delete m_poSRS;
                m_poSRS = nullptr;
            }
        }

        if (osGeometryType == "POINT")
            poFeatureDefn->SetGeomType(wkbPoint);
        else if (osGeometryType == "MULTIPOINT")
            poFeatureDefn->SetGeomType(wkbMultiPoint);
        else if (osGeometryType == "LINESTRING")
            poFeatureDefn->SetGeomType(wkbLineString);
        else if (osGeometryType == "MULTILINESTRING")
            poFeatureDefn->SetGeomType(wkbMultiLineString);
        else if (osGeometryType == "POLYGON")
            poFeatureDefn->SetGeomType(wkbPolygon);
        else if (osGeometryType == "MULTIPOLYGON")
            poFeatureDefn->SetGeomType(wkbMultiPolygon);

        /* --------------------------------------------------------------------
         */
        /*      Process a region line. */
        /* --------------------------------------------------------------------
         */
        if (osRegion.length() > 0)
        {
            char **papszTokens =
                CSLTokenizeStringComplex(osRegion.c_str(), "/", FALSE, FALSE);

            if (CSLCount(papszTokens) == 4)
            {
                sRegion.MinX = CPLAtofM(papszTokens[0]);
                sRegion.MaxX = CPLAtofM(papszTokens[1]);
                sRegion.MinY = CPLAtofM(papszTokens[2]);
                sRegion.MaxY = CPLAtofM(papszTokens[3]);
            }

            bRegionComplete = true;

            CSLDestroy(papszTokens);
        }

        /* --------------------------------------------------------------------
         */
        /*      Process fields. */
        /* --------------------------------------------------------------------
         */
        if (osFieldNames.length() || osFieldTypes.length())
        {
            char **papszFN =
                CSLTokenizeStringComplex(osFieldNames, "|", TRUE, TRUE);
            char **papszFT =
                CSLTokenizeStringComplex(osFieldTypes, "|", TRUE, TRUE);
            const int nFNCount = CSLCount(papszFN);
            const int nFTCount = CSLCount(papszFT);
            const int nFieldCount = std::max(nFNCount, nFTCount);

            for (int iField = 0; iField < nFieldCount; iField++)
            {
                OGRFieldDefn oField("", OFTString);

                if (iField < nFNCount)
                    oField.SetName(papszFN[iField]);
                else
                    oField.SetName(CPLString().Printf("Field_%d", iField + 1));

                if (iField < nFTCount)
                {
                    if (EQUAL(papszFT[iField], "integer"))
                        oField.SetType(OFTInteger);
                    else if (EQUAL(papszFT[iField], "double"))
                        oField.SetType(OFTReal);
                    else if (EQUAL(papszFT[iField], "datetime"))
                        oField.SetType(OFTDateTime);
                }

                poFeatureDefn->AddFieldDefn(&oField);
            }

            CSLDestroy(papszFN);
            CSLDestroy(papszFT);
        }
    }
    else
    {
        if (poSRS)
        {
            m_poSRS = poSRS->Clone();
            m_poSRS->SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);
        }
    }

    poFeatureDefn->GetGeomFieldDefn(0)->SetSpatialRef(m_poSRS);

    bValidFile = true;
}

/************************************************************************/
/*                           ~OGRGmtLayer()                           */
/************************************************************************/

OGRGmtLayer::~OGRGmtLayer()

{
    if (m_nFeaturesRead > 0 && poFeatureDefn != nullptr)
    {
        CPLDebug("Gmt", "%d features read on layer '%s'.",
                 static_cast<int>(m_nFeaturesRead), poFeatureDefn->GetName());
    }

    /* -------------------------------------------------------------------- */
    /*      Write out the region bounds if we know where they go, and we    */
    /*      are in update mode.                                             */
    /* -------------------------------------------------------------------- */
    if (nRegionOffset != 0 && bUpdate)
    {
        VSIFSeekL(m_fp, nRegionOffset, SEEK_SET);
        VSIFPrintfL(m_fp, "# @R%.12g/%.12g/%.12g/%.12g", sRegion.MinX,
                    sRegion.MaxX, sRegion.MinY, sRegion.MaxY);
    }

    /* -------------------------------------------------------------------- */
    /*      Clean up.                                                       */
    /* -------------------------------------------------------------------- */
    CSLDestroy(papszKeyedValues);

    if (poFeatureDefn)
        poFeatureDefn->Release();

    if (m_poSRS)
        m_poSRS->Release();

    if (m_fp != nullptr)
        VSIFCloseL(m_fp);
}

/************************************************************************/
/*                              ReadLine()                              */
/*                                                                      */
/*      Read a line into osLine.  If it is a comment line with @        */
/*      keyed values, parse out the keyed values into                   */
/*      papszKeyedValues.                                               */
/************************************************************************/

bool OGRGmtLayer::ReadLine()

{
    /* -------------------------------------------------------------------- */
    /*      Clear last line.                                                */
    /* -------------------------------------------------------------------- */
    osLine.erase();
    if (papszKeyedValues)
    {
        CSLDestroy(papszKeyedValues);
        papszKeyedValues = nullptr;
    }

    /* -------------------------------------------------------------------- */
    /*      Read newline.                                                   */
    /* -------------------------------------------------------------------- */
    const char *pszLine = CPLReadLineL(m_fp);
    if (pszLine == nullptr)
        return false;  // end of file.

    osLine = pszLine;

    /* -------------------------------------------------------------------- */
    /*      If this is a comment line with keyed values, parse them.        */
    /* -------------------------------------------------------------------- */

    if (osLine[0] != '#' || osLine.find_first_of('@') == std::string::npos)
        return true;

    CPLStringList aosKeyedValues;
    for (size_t i = 0; i < osLine.length(); i++)
    {
        if (osLine[i] == '@' && i + 2 <= osLine.size())
        {
            bool bInQuotes = false;

            size_t iValEnd = i + 2;  // Used after for.
            for (; iValEnd < osLine.length(); iValEnd++)
            {
                if (!bInQuotes &&
                    isspace(static_cast<unsigned char>(osLine[iValEnd])))
                    break;

                if (bInQuotes && iValEnd < osLine.length() - 1 &&
                    osLine[iValEnd] == '\\')
                {
                    iValEnd++;
                }
                else if (osLine[iValEnd] == '"')
                    bInQuotes = !bInQuotes;
            }

            const CPLString osValue = osLine.substr(i + 2, iValEnd - i - 2);

            // Unecape contents
            char *pszUEValue =
                CPLUnescapeString(osValue, nullptr, CPLES_BackslashQuotable);

            CPLString osKeyValue = osLine.substr(i + 1, 1);
            osKeyValue += pszUEValue;
            CPLFree(pszUEValue);
            aosKeyedValues.AddString(osKeyValue);

            i = iValEnd;
        }
    }
    papszKeyedValues = aosKeyedValues.StealList();

    return true;
}

/************************************************************************/
/*                            ResetReading()                            */
/************************************************************************/

void OGRGmtLayer::ResetReading()

{
    if (iNextFID == 0)
        return;

    iNextFID = 0;
    VSIFSeekL(m_fp, 0, SEEK_SET);
    ReadLine();
}

/************************************************************************/
/*                          ScanAheadForHole()                          */
/*                                                                      */
/*      Scan ahead to see if the next geometry is a hole.  If so        */
/*      return true, otherwise seek back to where we were and return    */
/*      false.                                                          */
/************************************************************************/

bool OGRGmtLayer::ScanAheadForHole()

{
    CPLString osSavedLine = osLine;
    const vsi_l_offset nSavedLocation = VSIFTellL(m_fp);

    while (ReadLine() && osLine[0] == '#')
    {
        if (papszKeyedValues != nullptr && papszKeyedValues[0][0] == 'H')
            return true;
    }

    VSIFSeekL(m_fp, nSavedLocation, SEEK_SET);
    osLine = std::move(osSavedLine);

    // We do not actually restore papszKeyedValues, but we
    // assume it does not matter since this method is only called
    // when processing the '>' line.

    return false;
}

/************************************************************************/
/*                           NextIsFeature()                            */
/*                                                                      */
/*      Returns true if the next line is a feature attribute line.      */
/*      This generally indicates the end of a multilinestring or        */
/*      multipolygon feature.                                           */
/************************************************************************/

bool OGRGmtLayer::NextIsFeature()

{
    CPLString osSavedLine = osLine;
    const vsi_l_offset nSavedLocation = VSIFTellL(m_fp);
    bool bReturn = false;

    ReadLine();

    if (osLine[0] == '#' && strstr(osLine, "@D") != nullptr)
        bReturn = true;

    VSIFSeekL(m_fp, nSavedLocation, SEEK_SET);
    osLine = std::move(osSavedLine);

    // We do not actually restore papszKeyedValues, but we
    // assume it does not matter since this method is only called
    // when processing the '>' line.

    return bReturn;
}

/************************************************************************/
/*                         GetNextRawFeature()                          */
/************************************************************************/

OGRFeature *OGRGmtLayer::GetNextRawFeature()

{
#if 0
    bool bMultiVertex =
        poFeatureDefn->GetGeomType() != wkbPoint
        && poFeatureDefn->GetGeomType() != wkbUnknown;
#endif
    CPLString osFieldData;
    OGRGeometry *poGeom = nullptr;

    /* -------------------------------------------------------------------- */
    /*      Read lines associated with this feature.                        */
    /* -------------------------------------------------------------------- */
    for (; true; ReadLine())
    {
        if (osLine.length() == 0)
            break;

        if (osLine[0] == '>')
        {
            OGRwkbGeometryType eType = wkbUnknown;
            if (poGeom)
                eType = wkbFlatten(poGeom->getGeometryType());
            if (eType == wkbMultiPolygon)
            {
                OGRMultiPolygon *poMP = poGeom->toMultiPolygon();
                if (ScanAheadForHole())
                {
                    // Add a hole to the current polygon.
                    poMP->getGeometryRef(poMP->getNumGeometries() - 1)
                        ->addRingDirectly(new OGRLinearRing());
                }
                else if (!NextIsFeature())
                {
                    OGRPolygon *poPoly = new OGRPolygon();

                    poPoly->addRingDirectly(new OGRLinearRing());

                    poMP->addGeometryDirectly(poPoly);
                }
                else
                    break; /* done geometry */
            }
            else if (eType == wkbPolygon)
            {
                if (ScanAheadForHole())
                    poGeom->toPolygon()->addRingDirectly(new OGRLinearRing());
                else
                    break; /* done geometry */
            }
            else if (eType == wkbMultiLineString && !NextIsFeature())
            {
                poGeom->toMultiLineString()->addGeometryDirectly(
                    new OGRLineString());
            }
            else if (poGeom != nullptr)
            {
                break;
            }
            else if (poFeatureDefn->GetGeomType() == wkbUnknown)
            {
                poFeatureDefn->SetGeomType(wkbLineString);
                // bMultiVertex = true;
            }
        }
        else if (osLine[0] == '#')
        {
            for (int i = 0;
                 papszKeyedValues != nullptr && papszKeyedValues[i] != nullptr;
                 i++)
            {
                if (papszKeyedValues[i][0] == 'D')
                    osFieldData = papszKeyedValues[i] + 1;
            }
        }
        else
        {
            // Parse point line.
            double dfX = 0.0;
            double dfY = 0.0;
            double dfZ = 0.0;
            const int nDim = CPLsscanf(osLine, "%lf %lf %lf", &dfX, &dfY, &dfZ);

            if (nDim >= 2)
            {
                if (poGeom == nullptr)
                {
                    switch (poFeatureDefn->GetGeomType())
                    {
                        case wkbLineString:
                            poGeom = new OGRLineString();
                            break;

                        case wkbPolygon:
                        {
                            OGRPolygon *poPoly = new OGRPolygon();
                            poGeom = poPoly;
                            poPoly->addRingDirectly(new OGRLinearRing());
                            break;
                        }

                        case wkbMultiPolygon:
                        {
                            OGRPolygon *poPoly = new OGRPolygon();
                            poPoly->addRingDirectly(new OGRLinearRing());

                            OGRMultiPolygon *poMP = new OGRMultiPolygon();
                            poGeom = poMP;
                            poMP->addGeometryDirectly(poPoly);
                        }
                        break;

                        case wkbMultiPoint:
                            poGeom = new OGRMultiPoint();
                            break;

                        case wkbMultiLineString:
                        {
                            OGRMultiLineString *poMLS =
                                new OGRMultiLineString();
                            poGeom = poMLS;
                            poMLS->addGeometryDirectly(new OGRLineString());
                            break;
                        }

                        case wkbPoint:
                        case wkbUnknown:
                        default:
                            poGeom = new OGRPoint();
                            break;
                    }
                }

                CPLAssert(poGeom != nullptr);
                // cppcheck-suppress nullPointerRedundantCheck
                switch (wkbFlatten(poGeom->getGeometryType()))
                {
                    case wkbPoint:
                    {
                        OGRPoint *poPoint = poGeom->toPoint();
                        poPoint->setX(dfX);
                        poPoint->setY(dfY);
                        if (nDim == 3)
                            poPoint->setZ(dfZ);
                        break;
                    }

                    case wkbLineString:
                    {
                        OGRLineString *poLS = poGeom->toLineString();
                        if (nDim == 3)
                            poLS->addPoint(dfX, dfY, dfZ);
                        else
                            poLS->addPoint(dfX, dfY);
                        break;
                    }

                    case wkbPolygon:
                    case wkbMultiPolygon:
                    {
                        OGRPolygon *poPoly = nullptr;

                        if (wkbFlatten(poGeom->getGeometryType()) ==
                            wkbMultiPolygon)
                        {
                            OGRMultiPolygon *poMP = poGeom->toMultiPolygon();
                            poPoly = poMP->getGeometryRef(
                                poMP->getNumGeometries() - 1);
                        }
                        else
                            poPoly = poGeom->toPolygon();

                        OGRLinearRing *poRing = nullptr;
                        if (poPoly->getNumInteriorRings() == 0)
                            poRing = poPoly->getExteriorRing();
                        else
                            poRing = poPoly->getInteriorRing(
                                poPoly->getNumInteriorRings() - 1);

                        if (nDim == 3)
                            poRing->addPoint(dfX, dfY, dfZ);
                        else
                            poRing->addPoint(dfX, dfY);
                    }
                    break;

                    case wkbMultiLineString:
                    {
                        OGRMultiLineString *poML = poGeom->toMultiLineString();
                        OGRLineString *poLine =
                            poML->getGeometryRef(poML->getNumGeometries() - 1);

                        if (nDim == 3)
                            poLine->addPoint(dfX, dfY, dfZ);
                        else
                            poLine->addPoint(dfX, dfY);
                    }
                    break;

                    default:
                        CPLAssert(false);
                }
            }
        }

        if (poGeom && wkbFlatten(poGeom->getGeometryType()) == wkbPoint)
        {
            ReadLine();
            break;
        }
    }

    if (poGeom == nullptr)
        return nullptr;

    /* -------------------------------------------------------------------- */
    /*      Create feature.                                                 */
    /* -------------------------------------------------------------------- */
    OGRFeature *poFeature = new OGRFeature(poFeatureDefn);
    poGeom->assignSpatialReference(m_poSRS);
    poFeature->SetGeometryDirectly(poGeom);
    poFeature->SetFID(iNextFID++);

    /* -------------------------------------------------------------------- */
    /*      Process field values.                                           */
    /* -------------------------------------------------------------------- */
    char **papszFD = CSLTokenizeStringComplex(osFieldData, "|", TRUE, TRUE);

    for (int iField = 0; papszFD != nullptr && papszFD[iField] != nullptr;
         iField++)
    {
        if (iField >= poFeatureDefn->GetFieldCount())
            break;

        poFeature->SetField(iField, papszFD[iField]);
    }

    CSLDestroy(papszFD);

    m_nFeaturesRead++;

    return poFeature;
}

/************************************************************************/
/*                           CompleteHeader()                           */
/*                                                                      */
/*      Finish writing out the header with field definitions and the    */
/*      layer geometry type.                                            */
/************************************************************************/

OGRErr OGRGmtLayer::CompleteHeader(OGRGeometry *poThisGeom)

{
    /* -------------------------------------------------------------------- */
    /*      If we do not already have a geometry type, try to work one      */
    /*      out and write it now.                                           */
    /* -------------------------------------------------------------------- */
    if (poFeatureDefn->GetGeomType() == wkbUnknown && poThisGeom != nullptr)
    {
        poFeatureDefn->SetGeomType(wkbFlatten(poThisGeom->getGeometryType()));

        const char *pszGeom = nullptr;
        switch (wkbFlatten(poFeatureDefn->GetGeomType()))
        {
            case wkbPoint:
                pszGeom = " @GPOINT";
                break;
            case wkbLineString:
                pszGeom = " @GLINESTRING";
                break;
            case wkbPolygon:
                pszGeom = " @GPOLYGON";
                break;
            case wkbMultiPoint:
                pszGeom = " @GMULTIPOINT";
                break;
            case wkbMultiLineString:
                pszGeom = " @GMULTILINESTRING";
                break;
            case wkbMultiPolygon:
                pszGeom = " @GMULTIPOLYGON";
                break;
            default:
                pszGeom = "";
                break;
        }

        VSIFPrintfL(m_fp, "#%s\n", pszGeom);
    }

    /* -------------------------------------------------------------------- */
    /*      Prepare and write the field names and types.                    */
    /* -------------------------------------------------------------------- */
    CPLString osFieldNames;
    CPLString osFieldTypes;

    for (int iField = 0; iField < poFeatureDefn->GetFieldCount(); iField++)
    {
        if (iField > 0)
        {
            osFieldNames += "|";
            osFieldTypes += "|";
        }

        osFieldNames += poFeatureDefn->GetFieldDefn(iField)->GetNameRef();
        switch (poFeatureDefn->GetFieldDefn(iField)->GetType())
        {
            case OFTInteger:
                osFieldTypes += "integer";
                break;

            case OFTReal:
                osFieldTypes += "double";
                break;

            case OFTDateTime:
                osFieldTypes += "datetime";
                break;

            default:
                osFieldTypes += "string";
                break;
        }
    }

    if (poFeatureDefn->GetFieldCount() > 0)
    {
        VSIFPrintfL(m_fp, "# @N%s\n", osFieldNames.c_str());
        VSIFPrintfL(m_fp, "# @T%s\n", osFieldTypes.c_str());
    }

    /* -------------------------------------------------------------------- */
    /*      Mark the end of the header, and start of feature data.          */
    /* -------------------------------------------------------------------- */
    VSIFPrintfL(m_fp, "# FEATURE_DATA\n");

    bHeaderComplete = true;
    bRegionComplete = true;  // no feature written, so we know them all!

    return OGRERR_NONE;
}

/************************************************************************/
/*                           ICreateFeature()                            */
/************************************************************************/

OGRErr OGRGmtLayer::ICreateFeature(OGRFeature *poFeature)

{
    if (!bUpdate)
    {
        CPLError(CE_Failure, CPLE_NoWriteAccess,
                 "Cannot create features on read-only dataset.");
        return OGRERR_FAILURE;
    }

    /* -------------------------------------------------------------------- */
    /*      Do we need to write the header describing the fields?           */
    /* -------------------------------------------------------------------- */
    if (!bHeaderComplete)
    {
        OGRErr eErr = CompleteHeader(poFeature->GetGeometryRef());

        if (eErr != OGRERR_NONE)
            return eErr;
    }

    /* -------------------------------------------------------------------- */
    /*      Write out the feature                                           */
    /* -------------------------------------------------------------------- */
    OGRGeometry *poGeom = poFeature->GetGeometryRef();

    if (poGeom == nullptr)
    {
        CPLError(CE_Failure, CPLE_AppDefined,
                 "Features without geometry not supported by GMT writer.");
        return OGRERR_FAILURE;
    }

    if (poFeatureDefn->GetGeomType() == wkbUnknown)
        poFeatureDefn->SetGeomType(wkbFlatten(poGeom->getGeometryType()));

    // Do we need a vertex collection marker grouping vertices.
    if (poFeatureDefn->GetGeomType() != wkbPoint)
        VSIFPrintfL(m_fp, ">\n");

    /* -------------------------------------------------------------------- */
    /*      Write feature properties()                                      */
    /* -------------------------------------------------------------------- */
    if (poFeatureDefn->GetFieldCount() > 0)
    {
        CPLString osFieldData;

        for (int iField = 0; iField < poFeatureDefn->GetFieldCount(); iField++)
        {
            OGRFieldType eFType =
                poFeatureDefn->GetFieldDefn(iField)->GetType();
            const char *pszRawValue = poFeature->GetFieldAsString(iField);

            if (iField > 0)
                osFieldData += "|";

            // We do not want prefix spaces for numeric values.
            if (eFType == OFTInteger || eFType == OFTReal)
                while (*pszRawValue == ' ')
                    pszRawValue++;

            if (strchr(pszRawValue, ' ') || strchr(pszRawValue, '|') ||
                strchr(pszRawValue, '\t') || strchr(pszRawValue, '\n'))
            {
                osFieldData += "\"";

                char *pszEscapedVal =
                    CPLEscapeString(pszRawValue, -1, CPLES_BackslashQuotable);
                osFieldData += pszEscapedVal;
                CPLFree(pszEscapedVal);

                osFieldData += "\"";
            }
            else
                osFieldData += pszRawValue;
        }

        VSIFPrintfL(m_fp, "# @D%s\n", osFieldData.c_str());
    }

    /* -------------------------------------------------------------------- */
    /*      Write Geometry                                                  */
    /* -------------------------------------------------------------------- */
    return WriteGeometry(OGRGeometry::ToHandle(poGeom), true);
}

/************************************************************************/
/*                           WriteGeometry()                            */
/*                                                                      */
/*      Write a geometry to the file.  If bHaveAngle is true it         */
/*      means the angle bracket preceding the point stream has          */
/*      already been written out.                                       */
/*                                                                      */
/*      We use the C API for geometry access because of its            */
/*      simplified access to vertices and children geometries.          */
/************************************************************************/

OGRErr OGRGmtLayer::WriteGeometry(OGRGeometryH hGeom, bool bHaveAngle)

{
    /* -------------------------------------------------------------------- */
    /*      This is a geometry with sub-geometries.                         */
    /* -------------------------------------------------------------------- */
    if (OGR_G_GetGeometryCount(hGeom) > 0)
    {
        OGRErr eErr = OGRERR_NONE;

        for (int iGeom = 0;
             iGeom < OGR_G_GetGeometryCount(hGeom) && eErr == OGRERR_NONE;
             iGeom++)
        {
            // We need to emit polygon @P and @H items while we still
            // know this is a polygon and which is the outer and inner
            // ring.
            if (wkbFlatten(OGR_G_GetGeometryType(hGeom)) == wkbPolygon)
            {
                if (!bHaveAngle)
                {
                    VSIFPrintfL(m_fp, ">\n");
                    bHaveAngle = true;
                }
                if (iGeom == 0)
                    VSIFPrintfL(m_fp, "# @P\n");
                else
                    VSIFPrintfL(m_fp, "# @H\n");
            }

            eErr =
                WriteGeometry(OGR_G_GetGeometryRef(hGeom, iGeom), bHaveAngle);
            bHaveAngle = false;
        }
        return eErr;
    }

    /* -------------------------------------------------------------------- */
    /*      If this is not a point we need to have an angle bracket to      */
    /*      mark the vertex list.                                           */
    /* -------------------------------------------------------------------- */
    if (wkbFlatten(OGR_G_GetGeometryType(hGeom)) != wkbPoint && !bHaveAngle)
        VSIFPrintfL(m_fp, ">\n");

    /* -------------------------------------------------------------------- */
    /*      Dump vertices.                                                  */
    /* -------------------------------------------------------------------- */
    const int nPointCount = OGR_G_GetPointCount(hGeom);
    const int nDim = OGR_G_GetCoordinateDimension(hGeom);
    // For testing only. Ticket #6453
    const bool bUseTab =
        CPLTestBool(CPLGetConfigOption("GMT_USE_TAB", "FALSE"));

    for (int iPoint = 0; iPoint < nPointCount; iPoint++)
    {
        const double dfX = OGR_G_GetX(hGeom, iPoint);
        const double dfY = OGR_G_GetY(hGeom, iPoint);
        const double dfZ = OGR_G_GetZ(hGeom, iPoint);

        sRegion.Merge(dfX, dfY);
        char szLine[128];
        OGRMakeWktCoordinate(szLine, dfX, dfY, dfZ, nDim);
        if (bUseTab)
        {
            for (char *szPtr = szLine; *szPtr != '\0'; ++szPtr)
            {
                if (*szPtr == ' ')
                    *szPtr = '\t';
            }
        }
        if (VSIFPrintfL(m_fp, "%s\n", szLine) < 1)
        {
            CPLError(CE_Failure, CPLE_FileIO, "Gmt write failure: %s",
                     VSIStrerror(errno));
            return OGRERR_FAILURE;
        }
    }

    return OGRERR_NONE;
}

/************************************************************************/
/*                            IGetExtent()                              */
/*                                                                      */
/*      Fetch extent of the data currently stored in the dataset.       */
/*      The bForce flag has no effect on SHO files since that value     */
/*      is always in the header.                                        */
/*                                                                      */
/*      Returns OGRERR_NONE/OGRRERR_FAILURE.                            */
/************************************************************************/

OGRErr OGRGmtLayer::IGetExtent(int iGeomField, OGREnvelope *psExtent,
                               bool bForce)

{
    if (bRegionComplete && sRegion.IsInit())
    {
        *psExtent = sRegion;
        return OGRERR_NONE;
    }

    return OGRLayer::IGetExtent(iGeomField, psExtent, bForce);
}

/************************************************************************/
/*                           TestCapability()                           */
/************************************************************************/

int OGRGmtLayer::TestCapability(const char *pszCap)

{
    if (EQUAL(pszCap, OLCRandomRead))
        return FALSE;

    if (EQUAL(pszCap, OLCSequentialWrite))
        return TRUE;

    if (EQUAL(pszCap, OLCFastSpatialFilter))
        return FALSE;

    if (EQUAL(pszCap, OLCFastGetExtent))
        return bRegionComplete;

    if (EQUAL(pszCap, OLCCreateField))
        return TRUE;

    if (EQUAL(pszCap, OLCZGeometries))
        return TRUE;

    return FALSE;
}

/************************************************************************/
/*                            CreateField()                             */
/************************************************************************/

OGRErr OGRGmtLayer::CreateField(const OGRFieldDefn *poField, int bApproxOK)

{
    if (!bUpdate)
    {
        CPLError(CE_Failure, CPLE_NoWriteAccess,
                 "Cannot create fields on read-only dataset.");
        return OGRERR_FAILURE;
    }

    if (bHeaderComplete)
    {
        CPLError(CE_Failure, CPLE_AppDefined,
                 "Unable to create fields after features have been created.");
        return OGRERR_FAILURE;
    }

    switch (poField->GetType())
    {
        case OFTInteger:
        case OFTReal:
        case OFTString:
        case OFTDateTime:
            poFeatureDefn->AddFieldDefn(poField);
            return OGRERR_NONE;
            break;

            break;

        default:
            if (!bApproxOK)
            {
                CPLError(CE_Failure, CPLE_AppDefined,
                         "Field %s is of unsupported type %s.",
                         poField->GetNameRef(),
                         poField->GetFieldTypeName(poField->GetType()));
                return OGRERR_FAILURE;
            }
            else if (poField->GetType() == OFTDate ||
                     poField->GetType() == OFTTime)
            {
                OGRFieldDefn oModDef(poField);
                oModDef.SetType(OFTDateTime);
                poFeatureDefn->AddFieldDefn(poField);
                return OGRERR_NONE;
            }
            else
            {
                OGRFieldDefn oModDef(poField);
                oModDef.SetType(OFTString);
                poFeatureDefn->AddFieldDefn(poField);
                return OGRERR_NONE;
            }
    }
}
