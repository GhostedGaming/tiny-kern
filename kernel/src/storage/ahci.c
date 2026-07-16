#include <stdint.h>
#include <logging/print.h>
#include <mm/hhdm.h>
#include <mm/page.h>
#include <mm/memory.h>
#include <pci.h>
#include <storage/ahci.h>

#define AHCI_GHC_AE   (1u << 31)
#define AHCI_GHC_IE   (1u << 1)
#define AHCI_GHC_HR   (1u << 0)

#define AHCI_BOHC_BOS (1u << 0)
#define AHCI_BOHC_OOS (1u << 1)
#define AHCI_BOHC_SOOE (1u << 2)
#define AHCI_BOHC_OOC (1u << 3)
#define AHCI_BOHC_BB  (1u << 4)

#define AHCI_CAP2_BOH (1u << 0)

#define PCI_CMD_IO_SPACE     (1u << 0)
#define PCI_CMD_MEM_SPACE    (1u << 1)
#define PCI_CMD_BUS_MASTER   (1u << 2)

#define AHCI_SPIN_TIMEOUT_ITERS 2000000

static HBA_MEM *g_abars[AHCI_MAX_CONTROLLERS];
static uint8_t  g_abar_count = 0;
static ahci_state_t g_ahci;
static drive_t g_drives[AHCI_MAX_CONTROLLERS][AHCI_MAX_PORTS];
static uint8_t g_identify_buf[512] __attribute__((aligned(4096)));

static int check_type(HBA_PORT *port) {
    uint32_t ssts = port->ssts;

    uint8_t ipm = (ssts >> 8) & 0x0F;
    uint8_t det = ssts & 0x0F;

    if (det != HBA_PORT_DET_PRESENT)
        return AHCI_DEV_NULL;
    if (ipm != HBA_PORT_IPM_ACTIVE)
        return AHCI_DEV_NULL;

    switch (port->sig) {
        case HBA_PORT_SIG_ATAPI:
            return AHCI_DEV_SATAPI;
        case HBA_PORT_SIG_SEMB:
            return AHCI_DEV_SEMB;
        case HBA_PORT_SIG_PM:
            return AHCI_DEV_PM;
        default:
            return AHCI_DEV_SATA;
    }
}

static uint64_t pci_read_bar64(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint32_t low = pci_config_read_dword(bus, slot, func, offset);
    if (low & 1)
        return 0;

    uint64_t addr_low  = (uint64_t)(low & 0xfffffff0u);
    uint64_t addr_high = (uint64_t)pci_config_read_dword(bus, slot, func, offset + 4);
    return (addr_high << 32) | addr_low;
}

static uint8_t pci_is_ahci_device(uint8_t bus, uint8_t slot, uint8_t func) {
    uint8_t class_code = pci_config_read_byte(bus, slot, func, 0x0b);
    uint8_t subclass   = pci_config_read_byte(bus, slot, func, 0x0a);
    uint8_t prog_if    = pci_config_read_byte(bus, slot, func, 0x09);

    return class_code == 0x01 && subclass == 0x06 && prog_if == 0x01;
}

static void pci_enable_device(uint8_t bus, uint8_t slot, uint8_t func) {
    uint16_t cmd = pci_config_read_word(bus, slot, func, 0x04);
    cmd |= PCI_CMD_MEM_SPACE | PCI_CMD_BUS_MASTER;
    pci_config_write_word(bus, slot, func, 0x04, cmd);
}

static void ahci_spin_delay(uint32_t iters) {
    for (volatile uint32_t i = 0; i < iters; i++) {
        asm volatile ("pause");
    }
}

static void ahci_bios_handoff(HBA_MEM *abar) {
    if (!(abar->cap2 & AHCI_CAP2_BOH)) {
        return;
    }

    abar->bohc |= AHCI_BOHC_OOS;

    uint32_t spin = 0;
    while ((abar->bohc & AHCI_BOHC_BOS) && spin++ < AHCI_SPIN_TIMEOUT_ITERS) {
        ahci_spin_delay(1);
    }

    ahci_spin_delay(250000);

    if (abar->bohc & AHCI_BOHC_BB) {
        ahci_spin_delay(2000000);
    }

    abar->bohc |= AHCI_BOHC_OOC;
}

static uint8_t ahci_hba_enable(HBA_MEM *abar) {
    abar->ghc |= AHCI_GHC_AE;

    uint32_t spin = 0;
    while (!(abar->ghc & AHCI_GHC_AE)) {
        if (++spin > AHCI_SPIN_TIMEOUT_ITERS) {
            return 1;
        }
    }

    return 0;
}

static uint8_t start_cmd(HBA_PORT *port) {
    uint32_t spin = 0;
    while (port->cmd & HBA_PxCMD_CR) {
        if (++spin > AHCI_SPIN_TIMEOUT_ITERS) {
            return 1;
        }
    }

    port->cmd |= HBA_PxCMD_FRE;
    port->cmd |= HBA_PxCMD_ST;
    return 0;
}

static uint8_t stop_cmd(HBA_PORT *port) {
    port->cmd &= ~HBA_PxCMD_ST;
    port->cmd &= ~HBA_PxCMD_FRE;

    uint32_t spin = 0;
    while (1) {
        if (!(port->cmd & HBA_PxCMD_FR) && !(port->cmd & HBA_PxCMD_CR)) {
            break;
        }
        if (++spin > AHCI_SPIN_TIMEOUT_ITERS) {
            return 1;
        }
    }

    return 0;
}

static uint8_t port_rebase(HBA_PORT *port, int portno) {
    if (stop_cmd(port) != 0) {
        print("AHCI: port %d failed to stop command engine, skipping\n", portno);
        return 1;
    }

    uint64_t phys_clb = AHCI_BASE + (portno << 10);
    uint64_t virt_clb = (uint64_t)phys_to_virt((uintptr_t)phys_clb);
    port->clb  = (uint32_t)(phys_clb & 0xFFFFFFFF);
    port->clbu = (uint32_t)(phys_clb >> 32);
    memset((void *)virt_clb, 0, 1024);

    uint64_t phys_fb = AHCI_BASE + (32 << 10) + (portno << 8);
    uint64_t virt_fb = (uint64_t)phys_to_virt((uintptr_t)phys_fb);
    port->fb  = (uint32_t)(phys_fb & 0xFFFFFFFF);
    port->fbu = (uint32_t)(phys_fb >> 32);
    memset((void *)virt_fb, 0, 256);

    port->is   = (uint32_t)-1;
    port->ie   = (uint32_t)-1;
    port->serr = (uint32_t)-1;
    port->ci   = 0;
    port->sact = 0;

    HBA_CMD_HEADER *cmdheader = (HBA_CMD_HEADER *)virt_clb;
    for (int i = 0; i < 32; i++) {
        uint64_t phys_ctba = AHCI_BASE + (40 << 10) + (portno << 13) + (i << 8);
        uint64_t virt_ctba = (uint64_t)phys_to_virt((uintptr_t)phys_ctba);
        cmdheader[i].prdtl = AHCI_CMD_TBL_PRDT_ENTRIES;
        cmdheader[i].ctba  = (uint32_t)(phys_ctba & 0xFFFFFFFF);
        cmdheader[i].ctbau = (uint32_t)(phys_ctba >> 32);
        memset((void *)virt_ctba, 0, 256);
    }

    if (start_cmd(port) != 0) {
        print("AHCI: port %d failed to start command engine\n", portno);
        return 1;
    }

    return 0;
}

static int find_cmdslot(HBA_PORT *port) {
    uint32_t slots = (port->sact | port->ci);
    for (int i = 0; i < AHCI_MAX_CMD_SLOTS; i++) {
        if ((slots & (1u << i)) == 0)
            return i;
    }
    return -1;
}

static uint8_t ahci_identify(uint8_t controller, uint8_t port) {
    if (controller >= g_abar_count || g_abars[controller] == NULL)
        return 1;

    HBA_PORT *hba_port = &g_abars[controller]->ports[port];
    hba_port->is   = (uint32_t)-1;
    hba_port->serr = (uint32_t)-1;

    int slot = find_cmdslot(hba_port);
    if (slot == -1)
        return 1;

    uint64_t clb_phys = (uint64_t)hba_port->clb | ((uint64_t)hba_port->clbu << 32);
    HBA_CMD_HEADER *cmdheader = (HBA_CMD_HEADER *)phys_to_virt(clb_phys) + slot;

    uint64_t ctba_phys = (uint64_t)cmdheader->ctba | ((uint64_t)cmdheader->ctbau << 32);

    uint32_t saved_ctba  = cmdheader->ctba;
    uint32_t saved_ctbau = cmdheader->ctbau;
    memset(cmdheader, 0, sizeof(*cmdheader));
    cmdheader->ctba  = saved_ctba;
    cmdheader->ctbau = saved_ctbau;
    cmdheader->cfl   = sizeof(FIS_REG_H2D) / sizeof(uint32_t);
    cmdheader->w     = 0;
    cmdheader->prdtl = 1;
    cmdheader->prdbc = 0;

    HBA_CMD_TBL *cmdtbl = (HBA_CMD_TBL *)phys_to_virt(ctba_phys);
    memset(cmdtbl, 0, sizeof(HBA_CMD_TBL));

    memset(g_identify_buf, 0, sizeof(g_identify_buf));
    uint64_t buf_phys = virt_to_phys(g_identify_buf);

    cmdtbl->prdt_entry[0].dba  = (uint32_t)(buf_phys & 0xFFFFFFFF);
    cmdtbl->prdt_entry[0].dbau = (uint32_t)(buf_phys >> 32);
    cmdtbl->prdt_entry[0].dbc  = sizeof(g_identify_buf) - 1;
    cmdtbl->prdt_entry[0].i    = 1;

    FIS_REG_H2D *cfis = (FIS_REG_H2D *)&cmdtbl->cfis;
    memset(cfis, 0, sizeof(*cfis));
    cfis->fis_type = FIS_TYPE_REG_H2D;
    cfis->c        = 1;
    cfis->command  = ATA_CMD_IDENTIFY;
    cfis->device   = 0;
    cfis->countl   = 0;
    cfis->counth   = 0;

    uint32_t spin = 0;
    while ((hba_port->tfd & (ATA_DEV_BUSY | ATA_DEV_DRQ)) != 0) {
        if (++spin > AHCI_SPIN_TIMEOUT_ITERS)
            return 1;
    }

    hba_port->ci |= 1u << slot;

    spin = 0;
    while ((hba_port->ci & (1u << slot)) != 0) {
        if (hba_port->is & (HBA_PxIS_TFES | HBA_PxIS_HBFS | HBA_PxIS_HBDS | HBA_PxIS_IFS))
            return 1;
        if (++spin > AHCI_SPIN_TIMEOUT_ITERS)
            return 1;
    }

    if (hba_port->tfd & (ATA_DEV_BUSY | ATA_DEV_DRQ))
        return 1;

    uint16_t *words = (uint16_t *)g_identify_buf;

    uint64_t lba48_sectors =
        (uint64_t)words[100] |
        ((uint64_t)words[101] << 16) |
        ((uint64_t)words[102] << 32) |
        ((uint64_t)words[103] << 48);

    uint32_t sector_size = AHCI_SECTOR_SIZE;
    if ((words[106] & (1 << 14)) && !(words[106] & (1 << 15)) && (words[106] & (1 << 12))) {
        uint32_t words_per_sector = (uint32_t)words[117] | ((uint32_t)words[118] << 16);
        sector_size = words_per_sector * 2;
    }

    drive_t *drive = &g_drives[controller][port];
    drive->sector_count = lba48_sectors;
    drive->sector_size  = sector_size;
    drive->controller   = controller;
    drive->port         = port;
    drive->device_type  = AHCI_DEV_SATA;

    return 0;
}

static void ahci_probe_ports(HBA_MEM *abar, uint8_t controller) {
    uint32_t pi = abar->pi;

    for (uint8_t i = 0; i < AHCI_MAX_PORTS; i++) {
        if ((pi & (1u << i)) == 0)
            continue;

        HBA_PORT *port = &abar->ports[i];
        int type = check_type(port);

        g_ahci.controllers[controller].ports[i].type = type;
        g_ahci.controllers[controller].ports[i].present = (type != AHCI_DEV_NULL);

        if (type == AHCI_DEV_NULL) {
            print("No device at controller %u port %u\n", controller, i);
            continue;
        }

        if (port_rebase(port, i) != 0) {
            g_ahci.controllers[controller].ports[i].present = 0;
            continue;
        }

        switch (type) {
            case AHCI_DEV_SATA:
                print("SATA drive found at controller %u port %u\n", controller, i);
                if (ahci_identify(controller, i) != 0) {
                    print("AHCI: identify failed on controller %u port %u\n", controller, i);
                } else {
                    print("AHCI: controller %u port %u sectors=%lu sector_size=%u\n",
                             controller, i,
                             (unsigned long)g_drives[controller][i].sector_count,
                             g_drives[controller][i].sector_size);
                }
                break;
            case AHCI_DEV_SATAPI:
                print("SATAPI drive found at controller %u port %u\n", controller, i);
                g_drives[controller][i].controller  = controller;
                g_drives[controller][i].port        = i;
                g_drives[controller][i].device_type = AHCI_DEV_SATAPI;
                g_drives[controller][i].sector_size = AHCI_SECTOR_SIZE;
                g_drives[controller][i].sector_count = 0;
                break;
            case AHCI_DEV_SEMB:
                print("SEMB device at controller %u port %u\n", controller, i);
                g_drives[controller][i].controller  = controller;
                g_drives[controller][i].port        = i;
                g_drives[controller][i].device_type = AHCI_DEV_SEMB;
                break;
            case AHCI_DEV_PM:
                print("Port multiplier at controller %u port %u\n", controller, i);
                g_drives[controller][i].controller  = controller;
                g_drives[controller][i].port        = i;
                g_drives[controller][i].device_type = AHCI_DEV_PM;
                break;
            default:
                break;
        }
    }
}

uint8_t ahci_read(uint8_t controller, uint8_t port, uint64_t sector, uint8_t count, void *buf) {
    if (count == 0)
        return 1;

    if (controller >= g_abar_count || g_abars[controller] == NULL)
        return 1;

    HBA_PORT *hba_port = &g_abars[controller]->ports[port];
    hba_port->is   = (uint32_t)-1;
    hba_port->serr = (uint32_t)-1;

    int slot = find_cmdslot(hba_port);
    if (slot == -1)
        return 1;

    int prdt_entries = (count + 15) / 16;
    if (prdt_entries > AHCI_MAX_PRDT_ENTRIES)
        return 1;

    uint64_t clb_phys = (uint64_t)hba_port->clb | ((uint64_t)hba_port->clbu << 32);
    HBA_CMD_HEADER *cmdheader = (HBA_CMD_HEADER *)phys_to_virt(clb_phys) + slot;

    uint64_t ctba_phys = (uint64_t)cmdheader->ctba | ((uint64_t)cmdheader->ctbau << 32);

    uint32_t saved_ctba  = cmdheader->ctba;
    uint32_t saved_ctbau = cmdheader->ctbau;
    memset(cmdheader, 0, sizeof(*cmdheader));
    cmdheader->ctba  = saved_ctba;
    cmdheader->ctbau = saved_ctbau;
    cmdheader->cfl   = sizeof(FIS_REG_H2D) / sizeof(uint32_t);
    cmdheader->w     = 0;
    cmdheader->prdtl = (uint16_t)prdt_entries;
    cmdheader->prdbc = 0;

    HBA_CMD_TBL *cmdtbl = (HBA_CMD_TBL *)phys_to_virt(ctba_phys);
    memset(cmdtbl, 0, sizeof(HBA_CMD_TBL) + (prdt_entries - 1) * sizeof(HBA_PRDT_ENTRY));

    uint8_t *buffer   = buf;
    uint8_t remaining = count;
    for (int i = 0; i < prdt_entries; i++) {
        uint32_t entry_sectors = remaining > 16 ? 16 : remaining;
        uint32_t entry_bytes   = entry_sectors * AHCI_SECTOR_SIZE;
        uint64_t buf_phys      = virt_to_phys(buffer);

        cmdtbl->prdt_entry[i].dba  = (uint32_t)(buf_phys & 0xFFFFFFFF);
        cmdtbl->prdt_entry[i].dbau = (uint32_t)(buf_phys >> 32);
        cmdtbl->prdt_entry[i].dbc  = entry_bytes - 1;
        cmdtbl->prdt_entry[i].i    = (i == prdt_entries - 1) ? 1 : 0;

        buffer    += entry_bytes;
        remaining -= entry_sectors;
    }

    FIS_REG_H2D *cfis = (FIS_REG_H2D *)&cmdtbl->cfis;
    memset(cfis, 0, sizeof(*cfis));
    cfis->fis_type = FIS_TYPE_REG_H2D;
    cfis->c        = 1;
    cfis->command  = ATA_CMD_READ_DMA_EXT;
    cfis->lba0     = (sector >>  0) & 0xFF;
    cfis->lba1     = (sector >>  8) & 0xFF;
    cfis->lba2     = (sector >> 16) & 0xFF;
    cfis->lba3     = (sector >> 24) & 0xFF;
    cfis->lba4     = 0;
    cfis->lba5     = 0;
    cfis->device   = 1 << 6;
    cfis->countl   = count & 0xFF;
    cfis->counth   = (count >> 8) & 0xFF;

    uint32_t spin = 0;
    while ((hba_port->tfd & (ATA_DEV_BUSY | ATA_DEV_DRQ)) != 0) {
        if (++spin > AHCI_SPIN_TIMEOUT_ITERS)
            return 1;
    }

    hba_port->ci |= 1u << slot;

    spin = 0;
    while ((hba_port->ci & (1u << slot)) != 0) {
        if (hba_port->is & (HBA_PxIS_TFES | HBA_PxIS_HBFS | HBA_PxIS_HBDS | HBA_PxIS_IFS))
            return 1;
        if (++spin > AHCI_SPIN_TIMEOUT_ITERS)
            return 1;
    }

    if (hba_port->tfd & (ATA_DEV_BUSY | ATA_DEV_DRQ))
        return 1;

    return 0;
}

uint8_t ahci_write(uint8_t controller, uint8_t port, uint64_t sector, uint8_t count, const void *buf) {
    if (count == 0)
        return 1;

    if (controller >= g_abar_count || g_abars[controller] == NULL)
        return 1;

    HBA_PORT *hba_port = &g_abars[controller]->ports[port];
    hba_port->is   = (uint32_t)-1;
    hba_port->serr = (uint32_t)-1;

    int slot = find_cmdslot(hba_port);
    if (slot == -1)
        return 1;

    int prdt_entries = (count + 15) / 16;
    if (prdt_entries > AHCI_MAX_PRDT_ENTRIES)
        return 1;

    uint64_t clb_phys = (uint64_t)hba_port->clb | ((uint64_t)hba_port->clbu << 32);
    HBA_CMD_HEADER *cmdheader = (HBA_CMD_HEADER *)phys_to_virt(clb_phys) + slot;

    uint64_t ctba_phys = (uint64_t)cmdheader->ctba | ((uint64_t)cmdheader->ctbau << 32);

    uint32_t saved_ctba  = cmdheader->ctba;
    uint32_t saved_ctbau = cmdheader->ctbau;
    memset(cmdheader, 0, sizeof(*cmdheader));
    cmdheader->ctba  = saved_ctba;
    cmdheader->ctbau = saved_ctbau;
    cmdheader->cfl   = sizeof(FIS_REG_H2D) / sizeof(uint32_t);
    cmdheader->w     = 1;
    cmdheader->prdtl = (uint16_t)prdt_entries;
    cmdheader->prdbc = 0;

    HBA_CMD_TBL *cmdtbl = (HBA_CMD_TBL *)phys_to_virt(ctba_phys);
    memset(cmdtbl, 0, sizeof(HBA_CMD_TBL) + (prdt_entries - 1) * sizeof(HBA_PRDT_ENTRY));

    const uint8_t *buffer = buf;
    uint8_t remaining     = count;
    for (int i = 0; i < prdt_entries; i++) {
        uint32_t entry_sectors = remaining > 16 ? 16 : remaining;
        uint32_t entry_bytes   = entry_sectors * AHCI_SECTOR_SIZE;
        uint64_t buf_phys      = virt_to_phys((void *)buffer);

        cmdtbl->prdt_entry[i].dba  = (uint32_t)(buf_phys & 0xFFFFFFFF);
        cmdtbl->prdt_entry[i].dbau = (uint32_t)(buf_phys >> 32);
        cmdtbl->prdt_entry[i].dbc  = entry_bytes - 1;
        cmdtbl->prdt_entry[i].i    = (i == prdt_entries - 1) ? 1 : 0;

        buffer    += entry_bytes;
        remaining -= entry_sectors;
    }

    FIS_REG_H2D *cfis = (FIS_REG_H2D *)&cmdtbl->cfis;
    memset(cfis, 0, sizeof(*cfis));
    cfis->fis_type = FIS_TYPE_REG_H2D;
    cfis->c        = 1;
    cfis->command  = ATA_CMD_WRITE_DMA_EXT;
    cfis->lba0     = (sector >>  0) & 0xFF;
    cfis->lba1     = (sector >>  8) & 0xFF;
    cfis->lba2     = (sector >> 16) & 0xFF;
    cfis->lba3     = (sector >> 24) & 0xFF;
    cfis->lba4     = 0;
    cfis->lba5     = 0;
    cfis->device   = 1 << 6;
    cfis->countl   = count & 0xFF;
    cfis->counth   = (count >> 8) & 0xFF;

    uint32_t spin = 0;
    while ((hba_port->tfd & (ATA_DEV_BUSY | ATA_DEV_DRQ)) != 0) {
        if (++spin > AHCI_SPIN_TIMEOUT_ITERS)
            return 1;
    }

    hba_port->ci |= 1u << slot;

    spin = 0;
    while ((hba_port->ci & (1u << slot)) != 0) {
        if (hba_port->is & (HBA_PxIS_TFES | HBA_PxIS_HBFS | HBA_PxIS_HBDS | HBA_PxIS_IFS))
            return 1;
        if (++spin > AHCI_SPIN_TIMEOUT_ITERS)
            return 1;
    }

    if (hba_port->tfd & (ATA_DEV_BUSY | ATA_DEV_DRQ))
        return 1;

    return 0;
}

uint8_t ahci_init() {
    memset(&g_ahci, 0, sizeof(g_ahci));
    memset(g_drives, 0, sizeof(g_drives));

    for (int i = 0; i < AHCI_MAX_CONTROLLERS; i++)
        g_abars[i] = NULL;

    g_abar_count = 0;

    for (uint16_t bus = 0; bus < 256; bus++) {
        for (uint8_t slot = 0; slot < 32; slot++) {
            uint8_t func_limit = 1;

            for (uint8_t func = 0; func < func_limit; func++) {
                uint16_t vendor = pci_config_read_word(bus, slot, func, 0);
                if (vendor == 0xffff)
                    continue;

                if (func == 0) {
                    uint8_t header_type = pci_config_read_byte(bus, slot, func, 0x0e);
                    if (header_type & 0x80)
                        func_limit = 8;
                }

                if (!pci_is_ahci_device(bus, slot, func))
                    continue;

                pci_enable_device(bus, slot, func);

                uint64_t abar_phys = pci_read_bar64(bus, slot, func, 0x24);
                if (abar_phys == 0)
                    continue;

                if (g_abar_count >= AHCI_MAX_CONTROLLERS) {
                    print("AHCI: max controllers reached, skipping %u:%u:%u\n",
                             bus, slot, func);
                    continue;
                }

                uint64_t abar_virt = (uint64_t)phys_to_virt((uintptr_t)abar_phys);
                uint64_t *pml4 = kernel_pml4;

                paging_map_page(pml4, (void *)abar_virt, abar_phys,
                                  PAGE_WRITABLE);

                paging_map_page(pml4, phys_to_virt(AHCI_BASE), AHCI_BASE,
                                 PAGE_WRITABLE);

                HBA_MEM *abar = (HBA_MEM *)abar_virt;

                ahci_bios_handoff(abar);

                if (ahci_hba_enable(abar) != 0) {
                    print("AHCI: controller at %u:%u:%u failed to enable, skipping\n",
                             bus, slot, func);
                    continue;
                }

                uint8_t ctrl_idx = g_abar_count;

                g_abars[ctrl_idx] = abar;
                g_ahci.controllers[ctrl_idx].abar = abar;

                print("AHCI controller %u found at %u:%u:%u base 0x%lx\n",
                         ctrl_idx, bus, slot, func, (unsigned long)abar_phys);

                ahci_probe_ports(abar, ctrl_idx);

                g_abar_count++;
            }
        }
    }

    g_ahci.count = g_abar_count;
    return g_abar_count;
}

uint8_t ahci_get_controller_count() {
    return g_ahci.count;
}

ahci_controller_t *ahci_get_controller(uint8_t index) {
    if (index >= g_ahci.count)
        return NULL;
    return &g_ahci.controllers[index];
}

uint8_t ahci_get_port_count(uint8_t controller) {
    if (controller >= g_ahci.count)
        return 0;

    uint8_t count = 0;
    for (uint8_t i = 0; i < AHCI_MAX_PORTS; i++) {
        if (g_ahci.controllers[controller].ports[i].present)
            count++;
    }
    return count;
}

uint8_t ahci_get_port_index(uint8_t controller, uint8_t n) {
    if (controller >= g_ahci.count)
        return 0xFF;

    uint8_t found = 0;
    for (uint8_t i = 0; i < AHCI_MAX_PORTS; i++) {
        if (g_ahci.controllers[controller].ports[i].present) {
            if (found == n)
                return i;
            found++;
        }
    }
    return 0xFF;
}

drive_t *ahci_get_drive(uint8_t controller, uint8_t port) {
    if (controller >= g_abar_count || port >= AHCI_MAX_PORTS)
        return NULL;
    if (!g_ahci.controllers[controller].ports[port].present)
        return NULL;
    return &g_drives[controller][port];
}