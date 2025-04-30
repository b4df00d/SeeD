#pragma once
void InitKnownComponents() { 

static bool initialized = false;

if(initialized) return;

initialized = true;

knownComponents.push_back(
 	{ "Entity", Components::Entity::mask, 
 		{
		}
 	}); 
knownComponents.push_back(
 	{ "Name", Components::Name::mask, 
 		{
		}
 	}); 
knownComponents.push_back(
 	{ "Shader", Components::Shader::mask, 
 		{
			{ "id", PropertyTypes::_assetID, Components::Entity::mask, 1, offsetof(Components::Shader, id) },
		}
 	}); 
knownComponents.push_back(
 	{ "Mesh", Components::Mesh::mask, 
 		{
			{ "id", PropertyTypes::_assetID, Components::Entity::mask, 1, offsetof(Components::Mesh, id) },
		}
 	}); 
knownComponents.push_back(
 	{ "Texture", Components::Texture::mask, 
 		{
			{ "id", PropertyTypes::_assetID, Components::Entity::mask, 1, offsetof(Components::Texture, id) },
		}
 	}); 
knownComponents.push_back(
 	{ "Material", Components::Material::mask, 
 		{
			{ "textures", PropertyTypes::_Handle, Components::Texture::mask, HLSL::MaterialTextureCount, offsetof(Components::Material, textures) },
			{ "prameters", PropertyTypes::_float, Components::Entity::mask, HLSL::MaterialParametersCount, offsetof(Components::Material, prameters) },
			{ "shader", PropertyTypes::_Handle, Components::Shader::mask, 1, offsetof(Components::Material, shader) },
		}
 	}); 
knownComponents.push_back(
 	{ "Transform", Components::Transform::mask, 
 		{
			{ "position", PropertyTypes::_float3, Components::Entity::mask, 1, offsetof(Components::Transform, position) },
			{ "rotation", PropertyTypes::_quaternion, Components::Entity::mask, 1, offsetof(Components::Transform, rotation) },
			{ "scale", PropertyTypes::_float3, Components::Entity::mask, 1, offsetof(Components::Transform, scale) },
		}
 	}); 
knownComponents.push_back(
 	{ "WorldMatrix", Components::WorldMatrix::mask, 
 		{
			{ "matrix", PropertyTypes::_float4x4, Components::Entity::mask, 1, offsetof(Components::WorldMatrix, matrix) },
		}
 	}); 
knownComponents.push_back(
 	{ "Instance", Components::Instance::mask, 
 		{
			{ "mesh", PropertyTypes::_Handle, Components::Mesh::mask, 1, offsetof(Components::Instance, mesh) },
			{ "material", PropertyTypes::_Handle, Components::Material::mask, 1, offsetof(Components::Instance, material) },
		}
 	}); 
knownComponents.push_back(
 	{ "Parent", Components::Parent::mask, 
 		{
			{ "entity", PropertyTypes::_Handle, Components::Entity::mask, 1, offsetof(Components::Parent, entity) },
		}
 	}); 
knownComponents.push_back(
 	{ "Light", Components::Light::mask, 
 		{
			{ "angle", PropertyTypes::_float, Components::Entity::mask, 1, offsetof(Components::Light, angle) },
			{ "range", PropertyTypes::_float, Components::Entity::mask, 1, offsetof(Components::Light, range) },
			{ "color", PropertyTypes::_float4, Components::Entity::mask, 1, offsetof(Components::Light, color) },
		}
 	}); 
knownComponents.push_back(
 	{ "Camera", Components::Camera::mask, 
 		{
			{ "fovY", PropertyTypes::_float, Components::Entity::mask, 1, offsetof(Components::Camera, fovY) },
			{ "nearClip", PropertyTypes::_float, Components::Entity::mask, 1, offsetof(Components::Camera, nearClip) },
			{ "farClip", PropertyTypes::_float, Components::Entity::mask, 1, offsetof(Components::Camera, farClip) },
		}
 	}); 
}

