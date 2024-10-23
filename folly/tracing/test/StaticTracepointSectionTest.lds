SECTIONS
{
  .stapsdt.base :
  {
    KEEP(*(.stapsdt.base))
  }
}
INSERT AFTER .text;
