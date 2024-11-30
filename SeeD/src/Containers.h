#pragma once

class Slots
{
public:
    std::vector<uint16_t> freeslots;
    void Start(uint16_t count)
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