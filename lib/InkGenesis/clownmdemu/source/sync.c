#include "sync.h"

void Sync_State_Initialise(Sync_State* const state)
{
	state->cycles_available = 0;
}

void Sync_Temporary_Initialise(Sync_Temporary* const temporary)
{
	temporary->current_cycle = 0;
}

void Sync_Update(Sync_State* const state, Sync_Temporary* const temporary, const cc_u32f target_cycle, const Sync_Callback callback, const void* const user_data)
{
	if (target_cycle > temporary->current_cycle + state->cycles_available)
	{
		const cc_s32f cycles_to_do = target_cycle - (temporary->current_cycle + state->cycles_available);

		if (cycles_to_do > 0)
		{
			temporary->current_cycle += state->cycles_available;
			state->cycles_available = (cc_s32f)callback((void*)user_data, cycles_to_do) - cycles_to_do;
		}

		temporary->current_cycle = target_cycle;
	}
}
