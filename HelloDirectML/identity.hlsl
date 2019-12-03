// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

RWBuffer<float> inputTensor : register(u0);
RWBuffer<float> outputTensor : register(u1);

#define BLOCK_SIZE 512
cbuffer ConstantBufferCS
{
    uint Height;
    uint Width;
    uint Channel;
};

[numthreads(BLOCK_SIZE, 1, 1)]
void identity(uint3 blockID : SV_GroupID, uint3 threadID : SV_GroupThreadID)
{
    uint index = blockID.x * BLOCK_SIZE + threadID.x;

    if (index < Width * Height * Channel)
    {
		outputTensor[index] = inputTensor[index];
    }
}
