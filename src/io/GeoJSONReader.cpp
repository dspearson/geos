/**********************************************************************
 *
 * GEOS - Geometry Engine Open Source
 * http://geos.osgeo.org
 *
 * Copyright (C) 2005-2006 Refractions Research Inc.
 *
 * This is free software; you can redistribute and/or modify it under
 * the terms of the GNU Lesser General Public Licence as published
 * by the Free Software Foundation.
 * See the COPYING file for more information.
 *
 **********************************************************************/

#include <geos/io/GeoJSONReader.h>
#include <geos/util/IllegalArgumentException.h>
#include <geos/io/ParseException.h>
#include <geos/geom/Coordinate.h>
#include <geos/geom/Point.h>
#include <geos/geom/LinearRing.h>
#include <geos/geom/LineString.h>
#include <geos/geom/Polygon.h>
#include <geos/geom/MultiPoint.h>
#include <geos/geom/MultiLineString.h>
#include <geos/geom/MultiPolygon.h>
#include <geos/geom/CoordinateSequence.h>
#include <geos/geom/CoordinateArraySequence.h>
#include <geos/geom/PrecisionModel.h>

#include <algorithm>
#include <ostream>
#include <sstream>
#include <cassert>

#include <json.hpp>

#undef DEBUG_GEOJSON_READER

using namespace geos::geom;
using json = nlohmann::json;

namespace geos {
namespace io { // geos.io

GeoJSONReader::GeoJSONReader(): GeoJSONReader(*(GeometryFactory::getDefaultInstance())) {}

GeoJSONReader::GeoJSONReader(const geom::GeometryFactory& gf) : geometryFactory(gf) {}

std::unique_ptr<geom::Geometry> GeoJSONReader::read(const std::string& geoJsonText) {
    try {
        json j = json::parse(geoJsonText);
        std::string type = j["type"];
        if (type == "Feature") {
            return readFeatureForGeometry(j);    
        } else if (type == "FeatureCollection") {
            return readFeatureCollectionForGeometry(j);    
        } else {
            return readGeometry(j);
        }
    } catch (json::exception ex) {
        throw ParseException("Error parsing JSON");
    }   
}

GeoJSONFeatureCollection GeoJSONReader::readFeatures(const std::string& geoJsonText) {
    try {
        json j = json::parse(geoJsonText);
        std::string type = j["type"];
        if (type == "Feature") {
            auto feature = readFeature(j);    
            return GeoJSONFeatureCollection { std::vector<GeoJSONFeature>{feature} };
        } else if (type == "FeatureCollection") {
            return readFeatureCollection(j);    
        } else {
            auto g = readGeometry(j);
            return GeoJSONFeatureCollection { std::vector<GeoJSONFeature>{GeoJSONFeature{std::move(g), std::map<std::string, GeoJSONValue>{} }}};
        }
    } catch (json::exception ex) {
        throw ParseException("Error parsing JSON");
    }
}

std::unique_ptr<geom::Geometry> GeoJSONReader::readFeatureForGeometry(const nlohmann::json& j) {
    auto geometryJson = j["geometry"];
    auto geometry = readGeometry(geometryJson);
    return geometry;
}

GeoJSONFeature GeoJSONReader::readFeature(const nlohmann::json& j) {
    auto geometryJson = j["geometry"];
    auto geometry = readGeometry(geometryJson);
    auto properties = j["properties"];
    std::map<std::string,GeoJSONValue> map = readProperties(properties);    
    GeoJSONFeature f = GeoJSONFeature{ std::move(geometry), std::move(map) };
    return f;
}

std::map<std::string,GeoJSONValue> GeoJSONReader::readProperties(const nlohmann::json& p) {
    std::map<std::string,GeoJSONValue> map;
    for(auto prop : p.items()) {
        map[prop.key()] = std::move(readProperty(prop.value()));
    }
    return map;
}

GeoJSONValue GeoJSONReader::readProperty(const nlohmann::json& value) {
    if (value.is_string()) {
        return GeoJSONValue { value.get<std::string>() };
    } else if (value.is_number()) {
        return GeoJSONValue { value.get<double>() };
    } else if (value.is_boolean()) {
        return GeoJSONValue { value.get<bool>() };
    } else if (value.is_array()) {
        std::vector<GeoJSONValue> v {};
        for (auto& el : value.items()) {
            const GeoJSONValue item = readProperty(el.value());
            v.push_back(item);
        }
        return GeoJSONValue{ v };
    } else if (value.is_object()) {
        std::map<std::string, GeoJSONValue> v {};
        for (auto& el : value.items()) {
            v[el.key()] = readProperty(el.value());
        }
        return GeoJSONValue{ v };
    } else {
        return GeoJSONValue{};
    }
}

std::unique_ptr<geom::Geometry> GeoJSONReader::readFeatureCollectionForGeometry(const nlohmann::json& j) {
    auto featuresJson = j["features"];
    std::vector<geom::Geometry *>* geometries = new std::vector<geom::Geometry *>();
    for(auto featureJson : featuresJson) {
        auto g = readFeatureForGeometry(featureJson);
        geometries->push_back(g.release());
    }
    return std::unique_ptr<geom::GeometryCollection>(geometryFactory.createGeometryCollection(geometries));
}

GeoJSONFeatureCollection GeoJSONReader::readFeatureCollection(const nlohmann::json& j) {
    auto featuresJson = j["features"];
    std::vector<GeoJSONFeature> features;
    for(auto featureJson : featuresJson) {
        auto f = readFeature(featureJson);
        features.push_back(f);
    }
    return GeoJSONFeatureCollection{features};
}


std::unique_ptr<geom::Geometry> GeoJSONReader::readGeometry(const nlohmann::json& j) {
    std::string type = j["type"];
    if (type == "Point") {
        return readPoint(j);
    } else if (type == "LineString") {
        return readLineString(j);
    } else if (type == "Polygon") {
        return readPolygon(j);
    } else if (type == "MultiPoint") {
        return readMultiPoint(j);
    } else if (type == "MultiLineString") {
        return readMultiLineString(j);
    } else if (type == "MultiPolygon") {
        return readMultiPolygon(j);
    } else if (type == "GeometryCollection") {
        return readGeometryCollection(j);
    } else {
        throw ParseException{"Unknown geometry type!"};
    }
}

std::unique_ptr<geom::Point> GeoJSONReader::readPoint(const nlohmann::json& j) {
    auto coords = j["coordinates"].get<std::vector<double>>();
    if (coords.size() == 1) {
        throw  ParseException("Expected two coordinates found one");
    } else if (coords.size() < 2) {
        return std::unique_ptr<geom::Point>(geometryFactory.createPoint(2));
    } else {
        geom::Coordinate coord = {coords[0], coords[1]};
        return std::unique_ptr<geom::Point>(geometryFactory.createPoint(coord));
    }
}

std::unique_ptr<geom::LineString> GeoJSONReader::readLineString(const nlohmann::json& j) {
    auto coords = j["coordinates"].get<std::vector<std::pair<double,double>>>();
    std::vector<geom::Coordinate> coordinates;
    for(const auto& coord : coords) {
        coordinates.push_back(geom::Coordinate{coord.first, coord.second});
    }
    geom::CoordinateArraySequence coordinateSequence { std::move(coordinates) };
    return std::unique_ptr<geom::LineString>(geometryFactory.createLineString(coordinateSequence));
}

std::unique_ptr<geom::Polygon> GeoJSONReader::readPolygon(const nlohmann::json& json) {
    auto polygonCoords = json["coordinates"].get<std::vector<std::vector<std::pair<double,double>>>>();
    std::vector<geom::LinearRing *> rings;
    for(const auto& ring : polygonCoords) {
        std::vector<geom::Coordinate> coordinates;
        for (const auto& coord : ring) {
            coordinates.push_back(geom::Coordinate{coord.first, coord.second});
        }
        geom::CoordinateArraySequence coordinateSequence { std::move(coordinates) };
        rings.push_back(geometryFactory.createLinearRing(std::move(coordinateSequence)));
    }
    if (rings.size() == 0) {
        return std::unique_ptr<geom::Polygon>(geometryFactory.createPolygon(2));
    } else if (rings.size() == 1) {
        geom::LinearRing* outerRing = rings[0];
        std::vector<geom::LinearRing *>* innerRings {};
        return std::unique_ptr<geom::Polygon>(geometryFactory.createPolygon(outerRing, innerRings));
    } else {
        geom::LinearRing* outerRing = rings[0];
        std::vector<geom::LinearRing *>* innerRings = new std::vector<geom::LinearRing *>(rings.begin() + 1, rings.end());
        return std::unique_ptr<geom::Polygon>(geometryFactory.createPolygon(outerRing, innerRings));        
    }
}

std::unique_ptr<geom::MultiPoint> GeoJSONReader::readMultiPoint(const nlohmann::json& j) {
    auto coords = j["coordinates"].get<std::vector<std::pair<double,double>>>();
    std::vector<geom::Coordinate> coordinates;
    for(const auto& coord : coords) {
        coordinates.push_back(geom::Coordinate{coord.first, coord.second});
    }
    geom::CoordinateArraySequence coordinateSequence { std::move(coordinates) };
    return std::unique_ptr<geom::MultiPoint>(geometryFactory.createMultiPoint(coordinateSequence));
}

std::unique_ptr<geom::MultiLineString> GeoJSONReader::readMultiLineString(const nlohmann::json& json) {
    auto listOfCoords = json["coordinates"].get<std::vector<std::vector<std::pair<double,double>>>>();
    auto lines = new std::vector<geom::Geometry *>{};
    for(const auto& coords :  listOfCoords) {    
        std::vector<geom::Coordinate> coordinates;
        for (const auto& coord : coords) {
            coordinates.push_back(geom::Coordinate{coord.first, coord.second});
        }
        geom::CoordinateArraySequence coordinateSequence { std::move(coordinates) };
        lines->push_back(geometryFactory.createLineString(std::move(coordinateSequence)));
    }
    return std::unique_ptr<geom::MultiLineString>(geometryFactory.createMultiLineString(lines));
}

std::unique_ptr<geom::MultiPolygon> GeoJSONReader::readMultiPolygon(const nlohmann::json& json) {
    auto multiPolygonCoords = json["coordinates"].get<std::vector<std::vector<std::vector<std::pair<double,double>>>>>();
    auto polygons = new std::vector<geom::Geometry *>();
    for(const auto& polygonCoords : multiPolygonCoords) {    
        std::vector<geom::LinearRing *> rings;
        for(const auto& ring : polygonCoords) {    
            std::vector<geom::Coordinate> coordinates;
            for(const auto& coord : ring) {    
                coordinates.push_back(geom::Coordinate{coord.first, coord.second});
            }
            geom::CoordinateArraySequence coordinateSequence { std::move(coordinates) };
            rings.push_back(geometryFactory.createLinearRing(std::move(coordinateSequence)));
        }
        if (rings.size() == 1) {
            geom::LinearRing* outerRing = rings[0];
            std::vector<geom::LinearRing *>* innerRings {};
            polygons->push_back(geometryFactory.createPolygon(outerRing, innerRings));
        } else {
            geom::LinearRing* outerRing = rings[0];
            std::vector<geom::LinearRing *>* innerRings = new std::vector<geom::LinearRing *>(rings.begin() + 1, rings.end());
            polygons->push_back(geometryFactory.createPolygon(outerRing, innerRings));        
        }
    }
    return std::unique_ptr<geom::MultiPolygon>(geometryFactory.createMultiPolygon(polygons));
}

std::unique_ptr<geom::GeometryCollection> GeoJSONReader::readGeometryCollection(const nlohmann::json& j) {
    std::vector<geom::Geometry *>* geometries = new std::vector<geom::Geometry *>();
    auto jsonGeometries = j["geometries"];
    for (const auto& jsonGeometry : jsonGeometries) {
        auto g = readGeometry(jsonGeometry);
        geometries->push_back(g.release());
    }
    return std::unique_ptr<geom::GeometryCollection>(geometryFactory.createGeometryCollection(geometries));
}

} // namespace geos.io
} // namespace geos