#include "scheduler.h"

#include "efm32.h"
#include "efm32_int.h"

#include <stdlib.h>
#include <stdbool.h>

#include "led.h"

/* variables */
static bool msp_in_use = true;
static task_table_t task_table[MAX_TASKS];
static uint32_t current_task = 0;
static void (*rt_task)(void);
static bool in_rt_task = false;

/* prototypes */
void SCHEDULER_TaskExit();
void SCHEDULER_RunRT();

/* functions */
void SCHEDULER_Init()
{
	
	INT_Disable();
	
	SysTick_Config(TASK_DURATION);
	
	int i;
	for (i = 0; i < MAX_TASKS; i++)
	{
		task_table[i].flags = 0;
	}
	
	rt_task = NULL;
	
}

void SCHEDULER_Run()
{
	
	SCHEDULER_Yield();
	INT_Enable();
	while(1);
	
}

void SCHEDULER_RunRTTask(void (*task)(void))
{
	
	rt_task = task;
	SCHEDULER_Yield();
	
}

bool SCHEDULER_TaskInit(task_t *task, void *entry_point)
{
	
	task->stack = (void*)(((uint32_t)task->stack_start) + TASK_STACK_SIZE - sizeof(hw_stack_frame_t));
	
	hw_stack_frame_t *process_frame = (hw_stack_frame_t*)(task->stack);
	process_frame->r0 = 0;
	process_frame->r1 = 0;
	process_frame->r2 = 0;
	process_frame->r3 = 0;
	process_frame->r12 = 0;
	process_frame->pc = (uint32_t)entry_point;
	process_frame->lr = (uint32_t)SCHEDULER_TaskExit;
	process_frame->psr = 0x21000000;
	
	int i;
	for (i = 0; i < MAX_TASKS; i++)
	{
		
		if (!(task_table[i].flags & IN_USE_FLAG))
		{
			
			task_table[i].task = task;
			task_table[i].flags = (IN_USE_FLAG | EXEC_FLAG);
			
			return true;
			
		}
		
	}
	
	return false;
	
}

void SCHEDULER_Wait(uint32_t flags)
{	
	
	task_table[current_task].flags |= flags;
	task_table[current_task].flags &= (~EXEC_FLAG);
	
	if (in_rt_task)
	{
		while (task_table[current_task].flags & flags);
	}
	else
	{
		SCHEDULER_Yield();
	}
	
}

void SCHEDULER_Release(uint32_t flags)
{

	int i;
	for (i = 0; i < MAX_TASKS; i++)
	{
		
		if (task_table[i].flags & flags)
		{
			
			task_table[i].flags &= (~flags);
			task_table[i].flags |= EXEC_FLAG;
			
		}
		
	}
	
}

void SCHEDULER_Yield()
{
	
	SCB->ICSR |= (1 << 28);
	
}

void SCHEDULER_TaskExit()
{
	
	task_table[current_task].flags = 0;
	SCHEDULER_Yield();
	while(1);
	
}

void SysTick_Handler()
{
	
	if (!msp_in_use)
	{
		__asm volatile (
			"STMIA %1!, {r4-r11}\n\t"
			"MRS %0, PSP\n\t"
				: "=r" (task_table[current_task].task->stack)
				: "r" (&task_table[current_task].task->sw_stack_frame)
		);
	}
	
	msp_in_use = false;
	
	do
	{

		current_task = (current_task + 1) % MAX_TASKS;

		if ((task_table[current_task].flags & (EXEC_FLAG | IN_USE_FLAG)) == (EXEC_FLAG | IN_USE_FLAG))
		{

			break;

		}

	}
	while(1);

	__asm volatile(
		"mov lr, #0xFFFFFFFD\n\t"
		"LDMIA %0!, {r4-r11}\n\t"
		"MSR PSP, %1\n\t"
		:
		: "r" (&task_table[current_task].task->sw_stack_frame),
			"r" (task_table[current_task].task->stack)
	);
	
}

void SCHEDULER_RunRT()
{
	
	in_rt_task = true;
	rt_task();
	in_rt_task = false;
	rt_task = NULL;
	
}

void PendSV_Handler()
{
	
	if (!msp_in_use)
	{
		__asm volatile (
			"STMIA %1!, {r4-r11}\n\t"
			"MRS %0, PSP\n\t"
				: "=r" (task_table[current_task].task->stack)
				: "r" (&task_table[current_task].task->sw_stack_frame)
		);
	}
	
	if (rt_task == NULL)
	{
		SysTick->VAL = 0; // reset the systick timer
		msp_in_use = false;
	
		do
		{

			current_task = (current_task + 1) % MAX_TASKS;

			if ((task_table[current_task].flags & (EXEC_FLAG | IN_USE_FLAG)) == (EXEC_FLAG | IN_USE_FLAG))
			{

				break;

			}

		}
		while(1);
	}
	else
	{
		SCHEDULER_RunRT();
	}
	
	__asm volatile(
		"mov lr, #0xFFFFFFFD\n\t"
		"LDMIA %0!, {r4-r11}\n\t"
		"MSR PSP, %1\n\t"
		:
		: "r" (&task_table[current_task].task->sw_stack_frame),
			"r" (task_table[current_task].task->stack)
	);
	
}
