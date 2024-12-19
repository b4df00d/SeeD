# Install script for directory: G:/Work/Dev/SeeD/Third/assimp-master/code

# Set the install prefix
if(NOT DEFINED CMAKE_INSTALL_PREFIX)
  set(CMAKE_INSTALL_PREFIX "G:/Work/Dev/SeeD/Third/assimp-master/out/install/x64-Debug")
endif()
string(REGEX REPLACE "/$" "" CMAKE_INSTALL_PREFIX "${CMAKE_INSTALL_PREFIX}")

# Set the install configuration name.
if(NOT DEFINED CMAKE_INSTALL_CONFIG_NAME)
  if(BUILD_TYPE)
    string(REGEX REPLACE "^[^A-Za-z0-9_]+" ""
           CMAKE_INSTALL_CONFIG_NAME "${BUILD_TYPE}")
  else()
    set(CMAKE_INSTALL_CONFIG_NAME "Debug")
  endif()
  message(STATUS "Install configuration: \"${CMAKE_INSTALL_CONFIG_NAME}\"")
endif()

# Set the component getting installed.
if(NOT CMAKE_INSTALL_COMPONENT)
  if(COMPONENT)
    message(STATUS "Install component: \"${COMPONENT}\"")
    set(CMAKE_INSTALL_COMPONENT "${COMPONENT}")
  else()
    set(CMAKE_INSTALL_COMPONENT)
  endif()
endif()

# Is this installation the result of a crosscompile?
if(NOT DEFINED CMAKE_CROSSCOMPILING)
  set(CMAKE_CROSSCOMPILING "FALSE")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "libassimp5.4.3-dev" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY OPTIONAL FILES "G:/Work/Dev/SeeD/Third/assimp-master/out/build/x64-Debug/lib/assimp-vc143-mtd.lib")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "libassimp5.4.3" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/bin" TYPE SHARED_LIBRARY FILES "G:/Work/Dev/SeeD/Third/assimp-master/out/build/x64-Debug/bin/assimp-vc143-mtd.dll")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "assimp-dev" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/assimp" TYPE FILE FILES
    "G:/Work/Dev/SeeD/Third/assimp-master/code/../include/assimp/anim.h"
    "G:/Work/Dev/SeeD/Third/assimp-master/code/../include/assimp/aabb.h"
    "G:/Work/Dev/SeeD/Third/assimp-master/code/../include/assimp/ai_assert.h"
    "G:/Work/Dev/SeeD/Third/assimp-master/code/../include/assimp/camera.h"
    "G:/Work/Dev/SeeD/Third/assimp-master/code/../include/assimp/color4.h"
    "G:/Work/Dev/SeeD/Third/assimp-master/code/../include/assimp/color4.inl"
    "G:/Work/Dev/SeeD/Third/assimp-master/out/build/x64-Debug/code/../include/assimp/config.h"
    "G:/Work/Dev/SeeD/Third/assimp-master/code/../include/assimp/ColladaMetaData.h"
    "G:/Work/Dev/SeeD/Third/assimp-master/code/../include/assimp/commonMetaData.h"
    "G:/Work/Dev/SeeD/Third/assimp-master/code/../include/assimp/defs.h"
    "G:/Work/Dev/SeeD/Third/assimp-master/code/../include/assimp/cfileio.h"
    "G:/Work/Dev/SeeD/Third/assimp-master/code/../include/assimp/light.h"
    "G:/Work/Dev/SeeD/Third/assimp-master/code/../include/assimp/material.h"
    "G:/Work/Dev/SeeD/Third/assimp-master/code/../include/assimp/material.inl"
    "G:/Work/Dev/SeeD/Third/assimp-master/code/../include/assimp/matrix3x3.h"
    "G:/Work/Dev/SeeD/Third/assimp-master/code/../include/assimp/matrix3x3.inl"
    "G:/Work/Dev/SeeD/Third/assimp-master/code/../include/assimp/matrix4x4.h"
    "G:/Work/Dev/SeeD/Third/assimp-master/code/../include/assimp/matrix4x4.inl"
    "G:/Work/Dev/SeeD/Third/assimp-master/code/../include/assimp/mesh.h"
    "G:/Work/Dev/SeeD/Third/assimp-master/code/../include/assimp/ObjMaterial.h"
    "G:/Work/Dev/SeeD/Third/assimp-master/code/../include/assimp/pbrmaterial.h"
    "G:/Work/Dev/SeeD/Third/assimp-master/code/../include/assimp/GltfMaterial.h"
    "G:/Work/Dev/SeeD/Third/assimp-master/code/../include/assimp/postprocess.h"
    "G:/Work/Dev/SeeD/Third/assimp-master/code/../include/assimp/quaternion.h"
    "G:/Work/Dev/SeeD/Third/assimp-master/code/../include/assimp/quaternion.inl"
    "G:/Work/Dev/SeeD/Third/assimp-master/code/../include/assimp/scene.h"
    "G:/Work/Dev/SeeD/Third/assimp-master/code/../include/assimp/metadata.h"
    "G:/Work/Dev/SeeD/Third/assimp-master/code/../include/assimp/texture.h"
    "G:/Work/Dev/SeeD/Third/assimp-master/code/../include/assimp/types.h"
    "G:/Work/Dev/SeeD/Third/assimp-master/code/../include/assimp/vector2.h"
    "G:/Work/Dev/SeeD/Third/assimp-master/code/../include/assimp/vector2.inl"
    "G:/Work/Dev/SeeD/Third/assimp-master/code/../include/assimp/vector3.h"
    "G:/Work/Dev/SeeD/Third/assimp-master/code/../include/assimp/vector3.inl"
    "G:/Work/Dev/SeeD/Third/assimp-master/code/../include/assimp/version.h"
    "G:/Work/Dev/SeeD/Third/assimp-master/code/../include/assimp/cimport.h"
    "G:/Work/Dev/SeeD/Third/assimp-master/code/../include/assimp/AssertHandler.h"
    "G:/Work/Dev/SeeD/Third/assimp-master/code/../include/assimp/importerdesc.h"
    "G:/Work/Dev/SeeD/Third/assimp-master/code/../include/assimp/Importer.hpp"
    "G:/Work/Dev/SeeD/Third/assimp-master/code/../include/assimp/DefaultLogger.hpp"
    "G:/Work/Dev/SeeD/Third/assimp-master/code/../include/assimp/ProgressHandler.hpp"
    "G:/Work/Dev/SeeD/Third/assimp-master/code/../include/assimp/IOStream.hpp"
    "G:/Work/Dev/SeeD/Third/assimp-master/code/../include/assimp/IOSystem.hpp"
    "G:/Work/Dev/SeeD/Third/assimp-master/code/../include/assimp/Logger.hpp"
    "G:/Work/Dev/SeeD/Third/assimp-master/code/../include/assimp/LogStream.hpp"
    "G:/Work/Dev/SeeD/Third/assimp-master/code/../include/assimp/NullLogger.hpp"
    "G:/Work/Dev/SeeD/Third/assimp-master/code/../include/assimp/cexport.h"
    "G:/Work/Dev/SeeD/Third/assimp-master/code/../include/assimp/Exporter.hpp"
    "G:/Work/Dev/SeeD/Third/assimp-master/code/../include/assimp/DefaultIOStream.h"
    "G:/Work/Dev/SeeD/Third/assimp-master/code/../include/assimp/DefaultIOSystem.h"
    "G:/Work/Dev/SeeD/Third/assimp-master/code/../include/assimp/ZipArchiveIOSystem.h"
    "G:/Work/Dev/SeeD/Third/assimp-master/code/../include/assimp/SceneCombiner.h"
    "G:/Work/Dev/SeeD/Third/assimp-master/code/../include/assimp/fast_atof.h"
    "G:/Work/Dev/SeeD/Third/assimp-master/code/../include/assimp/qnan.h"
    "G:/Work/Dev/SeeD/Third/assimp-master/code/../include/assimp/BaseImporter.h"
    "G:/Work/Dev/SeeD/Third/assimp-master/code/../include/assimp/Hash.h"
    "G:/Work/Dev/SeeD/Third/assimp-master/code/../include/assimp/MemoryIOWrapper.h"
    "G:/Work/Dev/SeeD/Third/assimp-master/code/../include/assimp/ParsingUtils.h"
    "G:/Work/Dev/SeeD/Third/assimp-master/code/../include/assimp/StreamReader.h"
    "G:/Work/Dev/SeeD/Third/assimp-master/code/../include/assimp/StreamWriter.h"
    "G:/Work/Dev/SeeD/Third/assimp-master/code/../include/assimp/StringComparison.h"
    "G:/Work/Dev/SeeD/Third/assimp-master/code/../include/assimp/StringUtils.h"
    "G:/Work/Dev/SeeD/Third/assimp-master/code/../include/assimp/SGSpatialSort.h"
    "G:/Work/Dev/SeeD/Third/assimp-master/code/../include/assimp/GenericProperty.h"
    "G:/Work/Dev/SeeD/Third/assimp-master/code/../include/assimp/SpatialSort.h"
    "G:/Work/Dev/SeeD/Third/assimp-master/code/../include/assimp/SkeletonMeshBuilder.h"
    "G:/Work/Dev/SeeD/Third/assimp-master/code/../include/assimp/SmallVector.h"
    "G:/Work/Dev/SeeD/Third/assimp-master/code/../include/assimp/SmoothingGroups.h"
    "G:/Work/Dev/SeeD/Third/assimp-master/code/../include/assimp/SmoothingGroups.inl"
    "G:/Work/Dev/SeeD/Third/assimp-master/code/../include/assimp/StandardShapes.h"
    "G:/Work/Dev/SeeD/Third/assimp-master/code/../include/assimp/RemoveComments.h"
    "G:/Work/Dev/SeeD/Third/assimp-master/code/../include/assimp/Subdivision.h"
    "G:/Work/Dev/SeeD/Third/assimp-master/code/../include/assimp/Vertex.h"
    "G:/Work/Dev/SeeD/Third/assimp-master/code/../include/assimp/LineSplitter.h"
    "G:/Work/Dev/SeeD/Third/assimp-master/code/../include/assimp/TinyFormatter.h"
    "G:/Work/Dev/SeeD/Third/assimp-master/code/../include/assimp/Profiler.h"
    "G:/Work/Dev/SeeD/Third/assimp-master/code/../include/assimp/LogAux.h"
    "G:/Work/Dev/SeeD/Third/assimp-master/code/../include/assimp/Bitmap.h"
    "G:/Work/Dev/SeeD/Third/assimp-master/code/../include/assimp/XMLTools.h"
    "G:/Work/Dev/SeeD/Third/assimp-master/code/../include/assimp/IOStreamBuffer.h"
    "G:/Work/Dev/SeeD/Third/assimp-master/code/../include/assimp/CreateAnimMesh.h"
    "G:/Work/Dev/SeeD/Third/assimp-master/code/../include/assimp/XmlParser.h"
    "G:/Work/Dev/SeeD/Third/assimp-master/code/../include/assimp/BlobIOSystem.h"
    "G:/Work/Dev/SeeD/Third/assimp-master/code/../include/assimp/MathFunctions.h"
    "G:/Work/Dev/SeeD/Third/assimp-master/code/../include/assimp/Exceptional.h"
    "G:/Work/Dev/SeeD/Third/assimp-master/code/../include/assimp/ByteSwapper.h"
    "G:/Work/Dev/SeeD/Third/assimp-master/code/../include/assimp/Base64.hpp"
    )
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "assimp-dev" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/assimp/Compiler" TYPE FILE FILES
    "G:/Work/Dev/SeeD/Third/assimp-master/code/../include/assimp/Compiler/pushpack1.h"
    "G:/Work/Dev/SeeD/Third/assimp-master/code/../include/assimp/Compiler/poppack1.h"
    "G:/Work/Dev/SeeD/Third/assimp-master/code/../include/assimp/Compiler/pstdint.h"
    )
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE FILE FILES "G:/Work/Dev/SeeD/Third/assimp-master/out/build/x64-Debug/bin/assimp-vc143-mtd.pdb")
endif()

