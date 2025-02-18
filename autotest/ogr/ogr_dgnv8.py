#!/usr/bin/env pytest
###############################################################################
#
# Project:  GDAL/OGR Test Suite
# Purpose:  Test DGNv8 Driver.
# Author:   Even Rouault <even dot rouault at spatialys dot com>
#
###############################################################################
# Copyright (c) 2017, Even Rouault <even dot rouault at spatialys dot com>
#
# SPDX-License-Identifier: MIT
###############################################################################

import os
import shutil

import gdaltest
import ogrtest
import pytest

from osgeo import gdal, ogr

pytestmark = pytest.mark.require_driver("DGNv8")

###############################################################################
# Compare with a reference CSV dump


def test_ogr_dgnv8_2():

    gdal.VectorTranslate(
        "/vsimem/ogr_dgnv8_2.csv",
        "data/dgnv8/test_dgnv8.dgn",
        options='-f CSV  -dsco geometry=as_wkt -sql "select *, ogr_style from my_model"',
    )

    ds_ref = ogr.Open("/vsimem/ogr_dgnv8_2.csv")
    lyr_ref = ds_ref.GetLayer(0)
    ds = ogr.Open("data/dgnv8/test_dgnv8_ref.csv")
    lyr = ds.GetLayer(0)
    ogrtest.compare_layers(lyr, lyr_ref, excluded_fields=["WKT"])

    gdal.Unlink("/vsimem/ogr_dgnv8_2.csv")


###############################################################################
# Run test_ogrsf


def test_ogr_dgnv8_3():

    import test_cli_utilities

    if test_cli_utilities.get_test_ogrsf_path() is None:
        pytest.skip()

    ret = gdaltest.runexternal(
        test_cli_utilities.get_test_ogrsf_path() + " -ro data/dgnv8/test_dgnv8.dgn"
    )

    assert ret.find("INFO") != -1 and ret.find("ERROR") == -1

    shutil.copy("data/dgnv8/test_dgnv8.dgn", "tmp/test_dgnv8.dgn")
    ret = gdaltest.runexternal(
        test_cli_utilities.get_test_ogrsf_path() + " tmp/test_dgnv8.dgn"
    )
    os.unlink("tmp/test_dgnv8.dgn")

    assert ret.find("INFO") != -1 and ret.find("ERROR") == -1


###############################################################################
# Test creation code


def test_ogr_dgnv8_4():

    tmp_dgn = "tmp/ogr_dgnv8_4.dgn"
    gdal.VectorTranslate(tmp_dgn, "data/dgnv8/test_dgnv8.dgn", format="DGNv8")

    tmp_csv = "/vsimem/ogr_dgnv8_4.csv"
    gdal.VectorTranslate(
        tmp_csv,
        tmp_dgn,
        options='-f CSV  -dsco geometry=as_wkt -sql "select *, ogr_style from my_model"',
    )
    gdal.Unlink(tmp_dgn)

    ds_ref = ogr.Open(tmp_csv)
    lyr_ref = ds_ref.GetLayer(0)
    ds = ogr.Open("data/dgnv8/test_dgnv8_write_ref.csv")
    lyr = ds.GetLayer(0)
    ogrtest.compare_layers(lyr, lyr_ref, excluded_fields=["WKT"])

    gdal.Unlink(tmp_csv)


###############################################################################
# Test creation options


def test_ogr_dgnv8_5():

    tmp_dgn = "tmp/ogr_dgnv8_5.dgn"
    options = [
        "APPLICATION=application",
        "TITLE=title",
        "SUBJECT=subject",
        "AUTHOR=author",
        "KEYWORDS=keywords",
        "TEMPLATE=template",
        "COMMENTS=comments",
        "LAST_SAVED_BY=last_saved_by",
        "REVISION_NUMBER=revision_number",
        "CATEGORY=category",
        "MANAGER=manager",
        "COMPANY=company",
    ]
    ds = ogr.GetDriverByName("DGNv8").CreateDataSource(tmp_dgn, options=options)
    lyr = ds.CreateLayer("my_layer")
    assert lyr.GetDataset().GetDescription() == ds.GetDescription()
    f = ogr.Feature(lyr.GetLayerDefn())
    f.SetGeometry(ogr.CreateGeometryFromWkt("POINT(0 1)"))
    lyr.CreateFeature(f)
    ds = None
    ds = ogr.Open(tmp_dgn)
    got_md = ds.GetMetadata_List("DGN")
    assert got_md == options
    ds = None

    tmp2_dgn = "tmp/ogr_dgnv8_5_2.dgn"
    ogr.GetDriverByName("DGNv8").CreateDataSource(
        tmp2_dgn, options=["SEED=" + tmp_dgn, "TITLE=another_title"]
    )
    ds = ogr.Open(tmp2_dgn)
    assert (
        ds.GetMetadataItem("TITLE", "DGN") == "another_title"
        and ds.GetMetadataItem("APPLICATION", "DGN") == "application"
    ), ds.GetMetadata("DGN")
    lyr = ds.GetLayer(0)
    assert lyr.GetName() == "my_layer"
    assert lyr.GetFeatureCount() == 0
    ds = None

    ds = ogr.GetDriverByName("DGNv8").CreateDataSource(
        tmp2_dgn, options=["SEED=" + tmp_dgn]
    )
    lyr = ds.CreateLayer("a_layer", options=["DESCRIPTION=my_layer", "DIM=2"])
    f = ogr.Feature(lyr.GetLayerDefn())
    f.SetGeometry(ogr.CreateGeometryFromWkt("POINT(2 3)"))
    lyr.CreateFeature(f)
    ds = None
    ds = ogr.Open(tmp2_dgn, update=1)
    lyr = ds.GetLayer(0)
    assert lyr.GetName() == "a_layer"
    assert lyr.GetFeatureCount() == 1
    f = lyr.GetNextFeature()
    if f.GetGeometryRef().ExportToWkt() != "POINT (2 3)":
        f.DumpReadable()
        pytest.fail()
    ds = None

    gdal.Unlink(tmp_dgn)
    gdal.Unlink(tmp2_dgn)
