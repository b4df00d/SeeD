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
		}
 	}); 
knownComponents.push_back(
 	{ "Shader", Components::Shader::mask, nullptr, 
 		{
			{ "id", PropertyTypes::_assetID, NULL, 1, offsetof(Components::Shader, id) },
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
 	{ "Instance", Components::Instance::mask, nullptr, 
 		{
			{ "mesh", PropertyTypes::_Handle, Components::Mesh::mask, 1, offsetof(Components::Instance, mesh) },
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
			{ "type", PropertyTypes::_uint, NULL, 1, offsetof(Components::Light, type) },
			{ "color", PropertyTypes::_float4, NULL, 1, offsetof(Components::Light, color) },
			{ "size", PropertyTypes::_float, NULL, 1, offsetof(Components::Light, size) },
			{ "range", PropertyTypes::_float, NULL, 1, offsetof(Components::Light, range) },
			{ "angle", PropertyTypes::_float, NULL, 1, offsetof(Components::Light, angle) },
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
}

