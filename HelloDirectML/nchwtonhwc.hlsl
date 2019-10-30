// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

RWBuffer<float> outputTensor : register(u0);

#define BLOCK_SIZE 512
cbuffer ConstantBufferCS
{
    uint Height;
    uint Width;
    uint Channel;
};

[numthreads(BLOCK_SIZE, 1, 1)]
void NCHWToNHWC(uint3 blockID : SV_GroupID, uint3 threadID : SV_GroupThreadID)
{
    uint index = blockID.x * BLOCK_SIZE + threadID.x;

    if (index < Width * Height * Channel)
    {
		outputTensor[index] = 1.2;
    }
}
