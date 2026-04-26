.crash_data (NOLOAD) :
{
    . = ALIGN(4);
    KEEP(*(.crash_data))
} >RAM
