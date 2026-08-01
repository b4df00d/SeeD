#pragma once
void InitKnownComponents() {

static bool initialized = false;

if(initialized) return;

initialized = true;

knownComponents.push_back(
 	{ "Entity", Components::Entity::mask, nullptr,
 		{
		}
 	});
knownComponents.push_back(
 	{ "Name", Components::Name::mask, nullptr,
 		{
			{ "name", PropertyTypes::_raw, NULL, ECS_NAME_LENGTH, offsetof(Components::Name, name) },
		}
 	});
knownComponents.push_back(
 	{ "Shader", Components::Shader::mask, Components::ShaderPropertyDraw,
 		{
			{ "id", PropertyTypes::_assetID, NULL, 1, offsetof(Components::Shader, id) },
			{ "path", PropertyTypes::_raw, NULL, ECS_SHADER_PATH, offsetof(Components::Shader, path) },
		}
 	});
knownComponents.push_back(
 	{ "Mesh", Components::Mesh::mask, nullptr,
 		{
			{ "id", PropertyTypes::_assetID, NULL, 1, offsetof(Components::Mesh, id) },
		}
 	});
knownComponents.push_back(
 	{ "Texture", Components::Texture::mask, nullptr,
 		{
			{ "id", PropertyTypes::_assetID, NULL, 1, offsetof(Components::Texture, id) },
		}
 	});
knownComponents.push_back(
 	{ "Material", Components::Material::mask, Components::MaterialPropertyDraw,
 		{
			{ "shader", PropertyTypes::_Handle, Components::Shader::mask, 1, offsetof(Components::Material, shader) },
			{ "textures", PropertyTypes::_Handle, Components::Texture::mask, HLSL::MaterialTextureCount, offsetof(Components::Material, textures) },
			{ "parameters", PropertyTypes::_float, NULL, HLSL::MaterialParametersCount, offsetof(Components::Material, parameters) },
		}
 	});
knownComponents.push_back(
 	{ "Transform", Components::Transform::mask, nullptr,
 		{
			{ "position", PropertyTypes::_float3, NULL, 1, offsetof(Components::Transform, position) },
			{ "rotation", PropertyTypes::_quaternion, NULL, 1, offsetof(Components::Transform, rotation) },
			{ "scale", PropertyTypes::_float3, NULL, 1, offsetof(Components::Transform, scale) },
		}
 	});
knownComponents.push_back(
 	{ "WorldMatrix", Components::WorldMatrix::mask, nullptr,
 		{
			{ "matrix", PropertyTypes::_float4x4, NULL, 1, offsetof(Components::WorldMatrix, matrix) },
		}
 	});
knownComponents.push_back(
 	{ "Instance", Components::Instance::mask, Components::InstancePropertyDraw,
 		{
			{ "mesh", PropertyTypes::_Handle, Components::Mesh::mask, 1, offsetof(Components::Instance, mesh) },
			{ "meshRT", PropertyTypes::_Handle, Components::Mesh::mask, 1, offsetof(Components::Instance, meshRT) },
			{ "material", PropertyTypes::_Handle, Components::Material::mask, 1, offsetof(Components::Instance, material) },
		}
 	});
knownComponents.push_back(
 	{ "Parent", Components::Parent::mask, nullptr,
 		{
			{ "entity", PropertyTypes::_Handle, Components::Entity::mask, 1, offsetof(Components::Parent, entity) },
		}
 	});
knownComponents.push_back(
 	{ "Light", Components::Light::mask, nullptr,
 		{
			{ "color", PropertyTypes::_float4, NULL, 1, offsetof(Components::Light, color) },
			{ "size", PropertyTypes::_float, NULL, 1, offsetof(Components::Light, size) },
			{ "range", PropertyTypes::_float, NULL, 1, offsetof(Components::Light, range) },
			{ "angle", PropertyTypes::_float, NULL, 1, offsetof(Components::Light, angle) },
			{ "type", PropertyTypes::_raw, NULL, 1, offsetof(Components::Light, type) },
			{ "castShadow", PropertyTypes::_bool, NULL, 1, offsetof(Components::Light, castShadow) },
		}
 	});
knownComponents.push_back(
 	{ "Camera", Components::Camera::mask, nullptr,
 		{
			{ "fovY", PropertyTypes::_float, NULL, 1, offsetof(Components::Camera, fovY) },
			{ "nearClip", PropertyTypes::_float, NULL, 1, offsetof(Components::Camera, nearClip) },
			{ "farClip", PropertyTypes::_float, NULL, 1, offsetof(Components::Camera, farClip) },
		}
 	});
knownComponents.push_back(
 	{ "Prefab", Components::Prefab::mask, Components::PrefabPropertyDraw,
 		{
			{ "file", PropertyTypes::_raw, NULL, ECS_PREFAB_PATH, offsetof(Components::Prefab, file) },
			{ "loadDistance", PropertyTypes::_float, NULL, 1, offsetof(Components::Prefab, loadDistance) },
		}
 	});
knownComponents.push_back(
 	{ "Terrain", Components::Terrain::mask, Components::TerrainPropertyDraw,
 		{
			{ "heightmap", PropertyTypes::_Handle, Components::Texture::mask, 1, offsetof(Components::Terrain, heightmap) },
			{ "gridMesh", PropertyTypes::_Handle, Components::Mesh::mask, 1, offsetof(Components::Terrain, gridMesh) },
			{ "material", PropertyTypes::_Handle, Components::Material::mask, 1, offsetof(Components::Terrain, material) },
			{ "useProceduralHeightmap", PropertyTypes::_uint, NULL, 1, offsetof(Components::Terrain, useProceduralHeightmap) },
			{ "noiseScale", PropertyTypes::_float, NULL, 1, offsetof(Components::Terrain, noiseScale) },
			{ "noiseOctaves", PropertyTypes::_uint, NULL, 1, offsetof(Components::Terrain, noiseOctaves) },
			{ "noiseLacunarity", PropertyTypes::_float, NULL, 1, offsetof(Components::Terrain, noiseLacunarity) },
			{ "noiseGain", PropertyTypes::_float, NULL, 1, offsetof(Components::Terrain, noiseGain) },
			{ "noiseSeed", PropertyTypes::_uint, NULL, 1, offsetof(Components::Terrain, noiseSeed) },
			{ "heightScale", PropertyTypes::_float, NULL, 1, offsetof(Components::Terrain, heightScale) },
			{ "heightOffset", PropertyTypes::_float, NULL, 1, offsetof(Components::Terrain, heightOffset) },
			{ "heightmapBlurRadius", PropertyTypes::_uint, NULL, 1, offsetof(Components::Terrain, heightmapBlurRadius) },
			{ "worldExtent", PropertyTypes::_float, NULL, 1, offsetof(Components::Terrain, worldExtent) },
			{ "quadtreeDepth", PropertyTypes::_uint, NULL, 1, offsetof(Components::Terrain, quadtreeDepth) },
			{ "targetScreenSize", PropertyTypes::_float, NULL, 1, offsetof(Components::Terrain, targetScreenSize) },
			{ "gridSpacing", PropertyTypes::_float, NULL, 1, offsetof(Components::Terrain, gridSpacing) },
			{ "gridSize", PropertyTypes::_float, NULL, 1, offsetof(Components::Terrain, gridSize) },
			{ "erosionEnabled", PropertyTypes::_uint, NULL, 1, offsetof(Components::Terrain, erosionEnabled) },
			{ "erosionOctaves", PropertyTypes::_uint, NULL, 1, offsetof(Components::Terrain, erosionOctaves) },
			{ "erosionScale", PropertyTypes::_float, NULL, 1, offsetof(Components::Terrain, erosionScale) },
			{ "erosionStrength", PropertyTypes::_float, NULL, 1, offsetof(Components::Terrain, erosionStrength) },
			{ "erosionGullyWeight", PropertyTypes::_float, NULL, 1, offsetof(Components::Terrain, erosionGullyWeight) },
			{ "erosionDetail", PropertyTypes::_float, NULL, 1, offsetof(Components::Terrain, erosionDetail) },
			{ "erosionLacunarity", PropertyTypes::_float, NULL, 1, offsetof(Components::Terrain, erosionLacunarity) },
			{ "erosionGain", PropertyTypes::_float, NULL, 1, offsetof(Components::Terrain, erosionGain) },
			{ "erosionCellScale", PropertyTypes::_float, NULL, 1, offsetof(Components::Terrain, erosionCellScale) },
			{ "erosionNormalization", PropertyTypes::_float, NULL, 1, offsetof(Components::Terrain, erosionNormalization) },
			{ "erosionRidgeRounding", PropertyTypes::_float, NULL, 1, offsetof(Components::Terrain, erosionRidgeRounding) },
			{ "erosionCreaseRounding", PropertyTypes::_float, NULL, 1, offsetof(Components::Terrain, erosionCreaseRounding) },
			{ "BVHMaxDepth", PropertyTypes::_uint, NULL, 1, offsetof(Components::Terrain, BVHMaxDepth) },
			{ "BVHMinRadius", PropertyTypes::_float, NULL, 1, offsetof(Components::Terrain, BVHMinRadius) },
			{ "BVHMaxRadius", PropertyTypes::_float, NULL, 1, offsetof(Components::Terrain, BVHMaxRadius) },
		}
 	});
knownComponents.push_back(
 	{ "MeshOverride", Components::MeshOverride::mask, nullptr,
 		{
			{ "overrideMeshIndex", PropertyTypes::_uint, NULL, 1, offsetof(Components::MeshOverride, overrideMeshIndex) },
			{ "sourceMeshIndex", PropertyTypes::_uint, NULL, 1, offsetof(Components::MeshOverride, sourceMeshIndex) },
			{ "dirty", PropertyTypes::_uint, NULL, 1, offsetof(Components::MeshOverride, dirty) },
		}
 	});
knownComponents.push_back(
 	{ "TerrainBaking", Components::TerrainBaking::mask, nullptr,
 		{
			{ "terrain", PropertyTypes::_Handle, Components::Terrain::mask, 1, offsetof(Components::TerrainBaking, terrain) },
		}
 	});
knownComponents.push_back(
 	{ "RenderSettingsVolume", Components::RenderSettingsVolume::mask, Components::RenderSettingsVolumePropertyDraw,
 		{
			{ "shape", PropertyTypes::_uint, NULL, 1, offsetof(Components::RenderSettingsVolume, shape) },
			{ "radius", PropertyTypes::_float, NULL, 1, offsetof(Components::RenderSettingsVolume, radius) },
			{ "blendDistance", PropertyTypes::_float, NULL, 1, offsetof(Components::RenderSettingsVolume, blendDistance) },
			{ "priority", PropertyTypes::_float, NULL, 1, offsetof(Components::RenderSettingsVolume, priority) },
			{ "overrides", PropertyTypes::_uint, NULL, 1, offsetof(Components::RenderSettingsVolume, overrides) },
			{ "density", PropertyTypes::_float, NULL, 1, offsetof(Components::RenderSettingsVolume, density) },
			{ "luminosity", PropertyTypes::_float, NULL, 1, offsetof(Components::RenderSettingsVolume, luminosity) },
			{ "specialNear", PropertyTypes::_float, NULL, 1, offsetof(Components::RenderSettingsVolume, specialNear) },
			{ "heightFalloff", PropertyTypes::_float, NULL, 1, offsetof(Components::RenderSettingsVolume, heightFalloff) },
			{ "noiseFrequency", PropertyTypes::_float, NULL, 1, offsetof(Components::RenderSettingsVolume, noiseFrequency) },
			{ "noiseThresholdLow", PropertyTypes::_float, NULL, 1, offsetof(Components::RenderSettingsVolume, noiseThresholdLow) },
			{ "noiseThresholdHigh", PropertyTypes::_float, NULL, 1, offsetof(Components::RenderSettingsVolume, noiseThresholdHigh) },
			{ "animationSpeed", PropertyTypes::_float, NULL, 1, offsetof(Components::RenderSettingsVolume, animationSpeed) },
			{ "expoMul", PropertyTypes::_float, NULL, 1, offsetof(Components::RenderSettingsVolume, expoMul) },
			{ "expoAdd", PropertyTypes::_float, NULL, 1, offsetof(Components::RenderSettingsVolume, expoAdd) },
			{ "P", PropertyTypes::_float, NULL, 1, offsetof(Components::RenderSettingsVolume, P) },
			{ "a", PropertyTypes::_float, NULL, 1, offsetof(Components::RenderSettingsVolume, a) },
			{ "m", PropertyTypes::_float, NULL, 1, offsetof(Components::RenderSettingsVolume, m) },
			{ "l", PropertyTypes::_float, NULL, 1, offsetof(Components::RenderSettingsVolume, l) },
			{ "c", PropertyTypes::_float, NULL, 1, offsetof(Components::RenderSettingsVolume, c) },
			{ "b", PropertyTypes::_float, NULL, 1, offsetof(Components::RenderSettingsVolume, b) },
		}
 	});
}

