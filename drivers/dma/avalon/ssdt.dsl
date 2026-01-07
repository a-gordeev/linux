DefinitionBlock ("", "SSDT", 2, "LINUX", "PROPS", 0x00000001)
{
    External (\_SB_.PCI0.S18, DeviceObj)

    Scope (\_SB.PCI0.S18)
    {
        Method (_DSD, 0, NotSerialized)
        {
            Return (Package ()
            {
                ToUUID ("daffd814-6eba-4d8c-8a91-bc9bbf4aa301"),
                Package ()
                {
                    Package () { "controller-base", 0 },
                    Package () { "read-status-desc-low", 0x80000000 },
                    Package () { "read-status-desc-high", 0 },
                    Package () { "write-status-desc-low", 0x80002000 },
                    Package () { "write-status-desc-hi", 0 },
                    Package () { "dma-mask-width", 64 },
                    Package () { "regs-bar", 0 },
                    Package () { "msi-index", 0 }
                }
            })
        }
    }
}
