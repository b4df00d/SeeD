#pragma once

class Slots
{
public:
    std::vector<uint16_t> freeslots;
    void On(uint16_t count)
    {
        freeslots.resize(count);
        for (uint16_t i = 0; i < count; i++)
        {
            freeslots[i] = (count - 1) - i;
        }
    }
    uint16_t Get()
    {
        assert(freeslots.size() > 0);
        uint16_t slot = freeslots[freeslots.size() - 1];
        freeslots.pop_back();
        return slot;
    }
    void Release(uint16_t slot)
    {
        freeslots.push_back(slot);
    }
};

template<typename keyType, typename cpuType, typename gpuType>
class Map
{
public:
    std::vector<keyType> keys;
    std::vector<cpuType> cpuData;
    std::vector<gpuType> gpuData;

    int Contains(keyType key)
    {
        for (int i = 0; i < keys.size(); i++)
        {
            if (keys[i] == key)
                return i;
        }
        return -1;
    }

    uint Add(keyType key, cpuType cpu, gpuType gpu)
    {
        keys.push_back(key);
        cpuData.push_back(cpu);
        gpuData.push_back(gpu);

        return keys.size() - 1;
    }

    cpuType& GetCPU(uint index)
    {
        return cpuData[index];
    }

    gpuType& GetGPU(uint index)
    {
        return gpuData[index];
    }

    void Remove(keyType key)
    {
        int index = Contains(key);
        keys[index] = keys.back();
        keys.pop_back();
        cpuData[index] = cpuData.back();
        cpuData.pop_back();
        gpuData[index] = gpuData.back();
        gpuData.pop_back();
    }
};