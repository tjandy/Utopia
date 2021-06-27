#pragma once

#include <UDX12/UDX12.h>

#include <UECS/Entity.hpp>

#include <vector>
#include <span>

namespace Ubpa::UECS {
	class World;
}

namespace Ubpa::Utopia {
	class IPipeline {
	public:
		struct InitDesc {
			size_t numFrame;
			ID3D12Device* device;
			ID3D12CommandQueue* cmdQueue;
		};
		struct CameraData {
			UECS::Entity entity;
			const UECS::World* world;
		};
		struct WorldCameraLink {
			std::vector<size_t> worldIndices;
			std::vector<size_t> cameraIndices;
		};

		virtual ~IPipeline() = default;

		virtual void Init(InitDesc desc) = 0;
		virtual void Render(
			std::span<const UECS::World* const> worlds,
			std::span<const CameraData> cameras,
			std::span<const WorldCameraLink> links,
			std::span<ID3D12Resource* const> defaultRTs
		) = 0;
	};
}
