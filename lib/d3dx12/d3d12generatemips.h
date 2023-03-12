#pragma once

#include <d3d12.h>

void D3D12GenerateMips(ID3D12Device* device, ID3D12CommandQueue* queue, ID3D12Resource* texture);
