/* Copyright (c) 2010, Code Aurora Forum. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 *
 */

#include <linux/delay.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/regulator/consumer.h>
#include <mach/gpio.h>
#include <mach/board.h>
#include <mach/camera-8x60.h>
#include <mach/vreg.h>
#include <mach/clk.h>
#include <mach/msm_bus.h>
#include <mach/msm_bus_board.h>
#include <mach/scm-io.h>
#include <mach/msm_iomap.h>

/* MIPI	CSI	controller registers */
#define	MIPI_PHY_CONTROL			0x00000000
#define	MIPI_PROTOCOL_CONTROL		0x00000004
#define	MIPI_INTERRUPT_STATUS		0x00000008
#define	MIPI_INTERRUPT_MASK			0x0000000C
#define	MIPI_CAMERA_CNTL			0x00000024
#define	MIPI_CALIBRATION_CONTROL	0x00000018
#define	MIPI_PHY_D0_CONTROL2		0x00000038
#define	MIPI_PHY_D1_CONTROL2		0x0000003C
#define	MIPI_PHY_D2_CONTROL2		0x00000040
#define	MIPI_PHY_D3_CONTROL2		0x00000044
#define	MIPI_PHY_CL_CONTROL			0x00000048
#define	MIPI_PHY_D0_CONTROL			0x00000034
#define	MIPI_PHY_D1_CONTROL			0x00000020
#define	MIPI_PHY_D2_CONTROL			0x0000002C
#define	MIPI_PHY_D3_CONTROL			0x00000030
#define	MIPI_PROTOCOL_CONTROL_SW_RST_BMSK			0x8000000
#define	MIPI_PROTOCOL_CONTROL_LONG_PACKET_HEADER_CAPTURE_BMSK	0x200000
#define	MIPI_PROTOCOL_CONTROL_DATA_FORMAT_BMSK			0x180000
#define	MIPI_PROTOCOL_CONTROL_DECODE_ID_BMSK			0x40000
#define	MIPI_PROTOCOL_CONTROL_ECC_EN_BMSK			0x20000
#define	MIPI_CALIBRATION_CONTROL_SWCAL_CAL_EN_SHFT		0x16
#define	MIPI_CALIBRATION_CONTROL_SWCAL_STRENGTH_OVERRIDE_EN_SHFT	0x15
#define	MIPI_CALIBRATION_CONTROL_CAL_SW_HW_MODE_SHFT		0x14
#define	MIPI_CALIBRATION_CONTROL_MANUAL_OVERRIDE_EN_SHFT	0x7
#define	MIPI_PROTOCOL_CONTROL_DATA_FORMAT_SHFT			0x13
#define	MIPI_PROTOCOL_CONTROL_DPCM_SCHEME_SHFT			0x1e
#define	MIPI_PHY_D0_CONTROL2_SETTLE_COUNT_SHFT			0x18
#define	MIPI_PHY_D0_CONTROL2_HS_TERM_IMP_SHFT			0x10
#define	MIPI_PHY_D0_CONTROL2_LP_REC_EN_SHFT				0x4
#define	MIPI_PHY_D0_CONTROL2_ERR_SOT_HS_EN_SHFT			0x3
#define	MIPI_PHY_D1_CONTROL2_SETTLE_COUNT_SHFT			0x18
#define	MIPI_PHY_D1_CONTROL2_HS_TERM_IMP_SHFT			0x10
#define	MIPI_PHY_D1_CONTROL2_LP_REC_EN_SHFT				0x4
#define	MIPI_PHY_D1_CONTROL2_ERR_SOT_HS_EN_SHFT			0x3
#define	MIPI_PHY_D2_CONTROL2_SETTLE_COUNT_SHFT			0x18
#define	MIPI_PHY_D2_CONTROL2_HS_TERM_IMP_SHFT			0x10
#define	MIPI_PHY_D2_CONTROL2_LP_REC_EN_SHFT				0x4
#define	MIPI_PHY_D2_CONTROL2_ERR_SOT_HS_EN_SHFT			0x3
#define	MIPI_PHY_D3_CONTROL2_SETTLE_COUNT_SHFT			0x18
#define	MIPI_PHY_D3_CONTROL2_HS_TERM_IMP_SHFT			0x10
#define	MIPI_PHY_D3_CONTROL2_LP_REC_EN_SHFT				0x4
#define	MIPI_PHY_D3_CONTROL2_ERR_SOT_HS_EN_SHFT			0x3
#define	MIPI_PHY_CL_CONTROL_HS_TERM_IMP_SHFT			0x18
#define	MIPI_PHY_CL_CONTROL_LP_REC_EN_SHFT				0x2
#define	MIPI_PHY_D0_CONTROL_HS_REC_EQ_SHFT				0x1c
#define	MIPI_PHY_D1_CONTROL_MIPI_CLK_PHY_SHUTDOWNB_SHFT		0x9
#define	MIPI_PHY_D1_CONTROL_MIPI_DATA_PHY_SHUTDOWNB_SHFT	0x8


#define MIPI_IMASK_ERROR_OCCUR                0xF01FFFC0
#define MIPI_IMASK_CLK_ULPM_ENTRY             (0x00000001<<0)
#define MIPI_IMASK_CLK_ULPM_EXIT              (0x00000001<<1)
#define MIPI_IMASK_DATA_ULPM_ENTRY            (0x00000001<<2)
#define MIPI_IMASK_DATA_ULPM_EXIT             (0x00000001<<3)
#define MIPI_IMASK_CLK_START                  (0x00000001<<4)
#define MIPI_IMASK_CLK_STOP                   (0x00000001<<5)
#define MIPI_IMASK_ERR_SOT                    (0x00000001<<6)
#define MIPI_IMASK_ERR_SOT_SYNC               (0x00000001<<7)
#define MIPI_IMASK_CLK_CTL_ERROR              (0x00000001<<8)
#define MIPI_IMASK_DATA_CTL_ERROR             (0x00000001<<9)
#define MIPI_IMASK_CLK_CMM_ERROR              (0x00000001<<10)
#define MIPI_IMASK_DATA_CMM_ERROR             (0x00000001<<11)
#define MIPI_IMASK_DL0_SYNC_ERROR             (0x00000001<<12)
#define MIPI_IMASK_DL1_SYNC_ERROR             (0x00000001<<13)
#define MIPI_IMASK_DL2_SYNC_ERROR             (0x00000001<<14)
#define MIPI_IMASK_DL3_SYNC_ERROR             (0x00000001<<15)
#define MIPI_IMASK_ECC_ERROR                  (0x00000001<<16)
#define MIPI_IMASK_CRC_ERROR                  (0x00000001<<17)
#define MIPI_IMASK_FRAME_SYNC_ERROR           (0x00000001<<18)
#define MIPI_IMASK_ID_ERROR                   (0x00000001<<19)
#define MIPI_IMASK_EOT_ERROR                  (0x00000001<<20)
#define MIPI_IMASK_SW_RST_DONE                (0x00000001<<21)
#define MIPI_IMASK_SHORT_PACKET_CAPTURE_DONE  (0x00000001<<22)
#define MIPI_IMASK_CAL_DONE                   (0x00000001<<23)
#define MIPI_IMASK_DL0_FIFO_OVERFLOW          (0x00000001<<28)
#define MIPI_IMASK_DL1_FIFO_OVERFLOW          (0x00000001<<29)
#define MIPI_IMASK_DL2_FIFO_OVERFLOW          (0x00000001<<30)
#define MIPI_IMASK_DL3_FIFO_OVERFLOW          (0x00000001<<31)

#define DEFAULT_MIPI_DRIVING_STRENGTH 0
#define DEFAULT_HS_IMPEDENCE 0x0F

static struct clk *camio_cam_clk;
static struct clk *camio_vfe_clk;
static struct clk *camio_csi_src_clk;
static struct clk *camio_csi0_vfe_clk;
static struct clk *camio_csi1_vfe_clk;
static struct clk *camio_csi0_clk;
static struct clk *camio_csi1_clk;
static struct clk *camio_csi0_pclk;
static struct clk *camio_csi1_pclk;
static struct clk *camio_vfe_pclk;
static struct clk *camio_jpeg_clk;
static struct clk *camio_jpeg_pclk;
static struct clk *camio_vpe_clk;
static struct clk *camio_vpe_pclk;
static struct regulator *fs_vfe;
static struct regulator *fs_ijpeg;
static struct regulator *fs_vpe;

static struct msm_camera_io_ext camio_ext;
static struct msm_camera_io_clk camio_clk;
static struct platform_device *camio_dev;
static struct resource *csiio;
void __iomem *csibase;
static int vpe_clk_rate;
#undef readl
#undef writel

#ifdef CONFIG_MSM_SECURE_IO
#define readl secure_readl
#define writel secure_writel
#else
#define readl readl
#define writel writel
#endif
/* HTC_START Max Sun 20110721 For debugging CSI error */
static atomic_t csi_irq_debug = ATOMIC_INIT(0);
static atomic_t csi_irq_debug_cnt = ATOMIC_INIT(0);
#define csi_irq_debug_max_cnt 200
/* HTC_END */
static uint8_t hs_impedence_shift = DEFAULT_HS_IMPEDENCE;
static uint8_t mipi_driving_strength_shift = DEFAULT_MIPI_DRIVING_STRENGTH;
static uint8_t settle_cnt = 0x14;

static struct msm_bus_vectors cam_init_vectors[] = {
	{
		.src = MSM_BUS_MASTER_VFE,
		.dst = MSM_BUS_SLAVE_SMI,
		.ab  = 0,
		.ib  = 0,
	},
	{
		.src = MSM_BUS_MASTER_VFE,
		.dst = MSM_BUS_SLAVE_EBI_CH0,
		.ab  = 0,
		.ib  = 0,
	},
	{
		.src = MSM_BUS_MASTER_VPE,
		.dst = MSM_BUS_SLAVE_SMI,
		.ab  = 0,
		.ib  = 0,
	},
	{
		.src = MSM_BUS_MASTER_VPE,
		.dst = MSM_BUS_SLAVE_EBI_CH0,
		.ab  = 0,
		.ib  = 0,
	},
	{
		.src = MSM_BUS_MASTER_JPEG_ENC,
		.dst = MSM_BUS_SLAVE_SMI,
		.ab  = 0,
		.ib  = 0,
	},
	{
		.src = MSM_BUS_MASTER_JPEG_ENC,
		.dst = MSM_BUS_SLAVE_EBI_CH0,
		.ab  = 0,
		.ib  = 0,
	},
};

static struct msm_bus_vectors cam_preview_vectors[] = {
	{
		.src = MSM_BUS_MASTER_VFE,
		.dst = MSM_BUS_SLAVE_SMI,
		.ab  = 0,
		.ib  = 0,
	},
	{
		.src = MSM_BUS_MASTER_VFE,
		.dst = MSM_BUS_SLAVE_EBI_CH0,
		.ab  = 283115520,
		.ib  = 679477248,
	},
	{
		.src = MSM_BUS_MASTER_VPE,
		.dst = MSM_BUS_SLAVE_SMI,
		.ab  = 0,
		.ib  = 0,
	},
	{
		.src = MSM_BUS_MASTER_VPE,
		.dst = MSM_BUS_SLAVE_EBI_CH0,
		.ab  = 0,
		.ib  = 0,
	},
	{
		.src = MSM_BUS_MASTER_JPEG_ENC,
		.dst = MSM_BUS_SLAVE_SMI,
		.ab  = 0,
		.ib  = 0,
	},
	{
		.src = MSM_BUS_MASTER_JPEG_ENC,
		.dst = MSM_BUS_SLAVE_EBI_CH0,
		.ab  = 0,
		.ib  = 0,
	},
};

static struct msm_bus_vectors cam_video_vectors[] = {
	{
		.src = MSM_BUS_MASTER_VFE,
		.dst = MSM_BUS_SLAVE_SMI,
		.ab  = 481844160,
		.ib  = 1156425984,
	},
	{
		.src = MSM_BUS_MASTER_VFE,
		.dst = MSM_BUS_SLAVE_EBI_CH0,
		.ab  = 481844160,
		.ib  = 1156425984,
	},
	{
		.src = MSM_BUS_MASTER_VPE,
		.dst = MSM_BUS_SLAVE_SMI,
		.ab  = 319610880,
		.ib  = 767066112,
	},
	{
		.src = MSM_BUS_MASTER_VPE,
		.dst = MSM_BUS_SLAVE_EBI_CH0,
		.ab  = 0,
		.ib  = 0,
	},
	{
		.src = MSM_BUS_MASTER_JPEG_ENC,
		.dst = MSM_BUS_SLAVE_SMI,
		.ab  = 0,
		.ib  = 0,
	},
	{
		.src = MSM_BUS_MASTER_JPEG_ENC,
		.dst = MSM_BUS_SLAVE_EBI_CH0,
		.ab  = 0,
		.ib  = 0,
	},
};

static struct msm_bus_vectors cam_snapshot_vectors[] = {
	{
		.src = MSM_BUS_MASTER_VFE,
		.dst = MSM_BUS_SLAVE_SMI,
		.ab  = 363686400,
		.ib  = 872847360,
	},
	{
		.src = MSM_BUS_MASTER_VFE,
		.dst = MSM_BUS_SLAVE_EBI_CH0,
		.ab  = 43315200,
		.ib  = 103956480,
	},
	{
		.src = MSM_BUS_MASTER_VPE,
		.dst = MSM_BUS_SLAVE_SMI,
		.ab  = 0,
		.ib  = 0,
	},
	{
		.src = MSM_BUS_MASTER_VPE,
		.dst = MSM_BUS_SLAVE_EBI_CH0,
		.ab  = 0,
		.ib  = 0,
	},
#ifdef CONFIG_MACH_VERDI_LTE
	{
		.src = MSM_BUS_MASTER_JPEG_ENC,
		.dst = MSM_BUS_SLAVE_SMI,
		.ab  = 0,
		.ib  = 0,
	},
	{
		.src = MSM_BUS_MASTER_JPEG_ENC,
		.dst = MSM_BUS_SLAVE_EBI_CH0,
		.ab  = 0,
		.ib  = 0,
	},
#else
	{
		.src = MSM_BUS_MASTER_JPEG_ENC,
		.dst = MSM_BUS_SLAVE_SMI,
		.ab  = 320864256,
		.ib  = 770074215,
	},
	{
		.src = MSM_BUS_MASTER_JPEG_ENC,
		.dst = MSM_BUS_SLAVE_EBI_CH0,
		.ab  = 320864256,
		.ib  = 770074215,
	},
#endif
};

static struct msm_bus_vectors cam_zsl_vectors[] = {
	{
		.src = MSM_BUS_MASTER_VFE,
		.dst = MSM_BUS_SLAVE_SMI,
		.ab  = 533406720,
		.ib  = 1280176128,
	},
	{
		.src = MSM_BUS_MASTER_VFE,
		.dst = MSM_BUS_SLAVE_EBI_CH0,
		.ab  = 660464640,
		.ib  = 1585115136,
	},
	{
		.src = MSM_BUS_MASTER_VPE,
		.dst = MSM_BUS_SLAVE_SMI,
		.ab  = 0,
		.ib  = 0,
	},
	{
		.src = MSM_BUS_MASTER_VPE,
		.dst = MSM_BUS_SLAVE_EBI_CH0,
		.ab  = 0,
		.ib  = 0,
	},
#ifdef CONFIG_MACH_VERDI_LTE
	{
		.src = MSM_BUS_MASTER_JPEG_ENC,
		.dst = MSM_BUS_SLAVE_SMI,
		.ab  = 0,
		.ib  = 0,
	},
	{
		.src = MSM_BUS_MASTER_JPEG_ENC,
		.dst = MSM_BUS_SLAVE_EBI_CH0,
		.ab  = 0,
		.ib  = 0,
	},
#else
	{
		.src = MSM_BUS_MASTER_JPEG_ENC,
		.dst = MSM_BUS_SLAVE_SMI,
		.ab  = 320864256,
		.ib  = 770074215,
	},
	{
		.src = MSM_BUS_MASTER_JPEG_ENC,
		.dst = MSM_BUS_SLAVE_EBI_CH0,
		.ab  = 320864256,
		.ib  = 770074215,
	},
#endif
};

static struct msm_bus_vectors cam_stereo_video_vectors[] = {
	{
		.src = MSM_BUS_MASTER_VFE,
		.dst = MSM_BUS_SLAVE_SMI,
		.ab  = 212336640,
		.ib  = 339738624,
	},
	{
		.src = MSM_BUS_MASTER_VFE,
		.dst = MSM_BUS_SLAVE_EBI_CH0,
		.ab  = 25090560,
		.ib  = 40144896,
	},
	{
		.src = MSM_BUS_MASTER_VPE,
		.dst = MSM_BUS_SLAVE_SMI,
		.ab  = 239708160,
		.ib  = 383533056,
	},
	{
		.src = MSM_BUS_MASTER_VPE,
		.dst = MSM_BUS_SLAVE_EBI_CH0,
		.ab  = 79902720,
		.ib  = 127844352,
	},
	{
		.src = MSM_BUS_MASTER_JPEG_ENC,
		.dst = MSM_BUS_SLAVE_SMI,
		.ab  = 0,
		.ib  = 0,
	},
	{
		.src = MSM_BUS_MASTER_JPEG_ENC,
		.dst = MSM_BUS_SLAVE_EBI_CH0,
		.ab  = 0,
		.ib  = 0,
	},
};

static struct msm_bus_vectors cam_stereo_snapshot_vectors[] = {
	{
		.src = MSM_BUS_MASTER_VFE,
		.dst = MSM_BUS_SLAVE_SMI,
		.ab  = 0,
		.ib  = 0,
	},
	{
		.src = MSM_BUS_MASTER_VFE,
		.dst = MSM_BUS_SLAVE_EBI_CH0,
		.ab  = 300902400,
		.ib  = 481443840,
	},
	{
		.src = MSM_BUS_MASTER_VPE,
		.dst = MSM_BUS_SLAVE_SMI,
		.ab  = 230307840,
		.ib  = 368492544,
	},
	{
		.src = MSM_BUS_MASTER_VPE,
		.dst = MSM_BUS_SLAVE_EBI_CH0,
		.ab  = 245113344,
		.ib  = 392181351,
	},
	{
		.src = MSM_BUS_MASTER_JPEG_ENC,
		.dst = MSM_BUS_SLAVE_SMI,
		.ab  = 106536960,
		.ib  = 170459136,
	},
	{
		.src = MSM_BUS_MASTER_JPEG_ENC,
		.dst = MSM_BUS_SLAVE_EBI_CH0,
		.ab  = 106536960,
		.ib  = 170459136,
	},
};

static struct msm_bus_paths cam_bus_client_config[] = {
	{
		ARRAY_SIZE(cam_init_vectors),
		cam_init_vectors,
	},
	{
		ARRAY_SIZE(cam_preview_vectors),
		cam_preview_vectors,
	},
	{
		ARRAY_SIZE(cam_video_vectors),
		cam_video_vectors,
	},
	{
		ARRAY_SIZE(cam_snapshot_vectors),
		cam_snapshot_vectors,
	},
	{
		ARRAY_SIZE(cam_zsl_vectors),
		cam_zsl_vectors,
	},
	{
		ARRAY_SIZE(cam_stereo_video_vectors),
		cam_stereo_video_vectors,
	},
	{
		ARRAY_SIZE(cam_stereo_snapshot_vectors),
		cam_stereo_snapshot_vectors,
	},
};

static struct msm_bus_scale_pdata cam_bus_client_pdata = {
		cam_bus_client_config,
		ARRAY_SIZE(cam_bus_client_config),
		.name = "msm_camera",
};


void msm_io_w(u32 data, void __iomem *addr)
{
	CDBG("[CAM] %s: %08x %08x\n", __func__, (int) (addr), (data));
	writel((data), (addr));
}

void msm_io_w_mb(u32 data, void __iomem *addr)
{
	CDBG("[CAM] %s: %08x %08x\n", __func__, (int) (addr), (data));
	wmb();
	writel((data), (addr));
	wmb();
}

u32 msm_io_r(void __iomem *addr)
{
	uint32_t data = readl(addr);
	CDBG("[CAM] %s: %08x %08x\n", __func__, (int) (addr), (data));
	return data;
}

u32 msm_io_r_mb(void __iomem *addr)
{
	uint32_t data;
	rmb();
	data = readl(addr);
	rmb();
	CDBG("[CAM] %s: %08x %08x\n", __func__, (int) (addr), (data));
	return data;
}

void msm_io_memcpy_toio(void __iomem *dest_addr,
	void __iomem *src_addr, u32 len)
{
	int i;
	u32 *d = (u32 *) dest_addr;
	u32 *s = (u32 *) src_addr;
	/* memcpy_toio does not work. Use writel for now */
	for (i = 0; i < len; i++)
		writel(*s++, d++);
}

void msm_io_dump(void __iomem *addr, int size)
{
	char line_str[128], *p_str;
	int i;
	u32 *p = (u32 *) addr;
	u32 data;
	CDBG("[CAM] %s: %p %d\n", __func__, addr, size);
	line_str[0] = '\0';
	p_str = line_str;
	for (i = 0; i < size/4; i++) {
		if (i % 4 == 0) {
			sprintf(p_str, "%08x: ", (u32) p);
			p_str += 10;
		}
		data = readl(p++);
		sprintf(p_str, "%08x ", data);
		p_str += 9;
		if ((i + 1) % 4 == 0) {
			CDBG("[CAM] %s\n", line_str);
			line_str[0] = '\0';
			p_str = line_str;
		}
	}
	if (line_str[0] != '\0')
		CDBG("[CAM] %s\n", line_str);
}

void msm_io_memcpy(void __iomem *dest_addr, void __iomem *src_addr, u32 len)
{
	CDBG("[CAM] %s: %p %p %d\n", __func__, dest_addr, src_addr, len);
	msm_io_memcpy_toio(dest_addr, src_addr, len / 4);
	msm_io_dump(dest_addr, len);
}

static void msm_camera_fs_enable(void)
{



	fs_vfe = regulator_get(NULL, "fs_vfe");
	if (IS_ERR(fs_vfe)) {
		CDBG("[CAM] %s: Regulator FS_VFE get failed %ld\n", __func__,
			PTR_ERR(fs_vfe));
		fs_vfe = NULL;
	} else if (regulator_enable(fs_vfe)) {
		CDBG("[CAM] %s: Regulator FS_VFE enable failed\n", __func__);
		regulator_put(fs_vfe);
	}
	return;
}

static void msm_camera_fs_disable(void)
{
	if (fs_vfe) {
		regulator_disable(fs_vfe);
		regulator_put(fs_vfe);
		fs_vfe = NULL;
	}
}

int msm_camio_clk_enable(enum msm_camio_clk_type clktype)
{
	int rc = 0;
	struct clk *clk = NULL;
	pr_info("[CAM] %s clktype:%d", __func__, clktype);
	switch (clktype) {
	case CAMIO_CAM_MCLK_CLK:
		camio_cam_clk =
		clk = clk_get(NULL, "cam_clk");
		pr_info("[CAM] %s clk:0x%x", __func__, (unsigned int)clk);
		msm_camio_clk_rate_set_2(clk, camio_clk.mclk_clk_rate);
		break;

	case CAMIO_VFE_CLK:
		camio_vfe_clk =
		clk = clk_get(NULL, "vfe_clk");
		msm_camio_clk_rate_set_2(clk, camio_clk.vfe_clk_rate);
		break;

	case CAMIO_CSI0_VFE_CLK:
		camio_csi0_vfe_clk =
		clk = clk_get(NULL, "csi_vfe_clk");
		break;

	case CAMIO_CSI1_VFE_CLK:
		camio_csi1_vfe_clk =
		clk = clk_get(&camio_dev->dev, "csi_vfe_clk");
		break;

	case CAMIO_CSI_SRC_CLK:
		camio_csi_src_clk =
		clk = clk_get(NULL, "csi_src_clk");
		msm_camio_clk_rate_set_2(clk, 384000000);
		break;

	case CAMIO_CSI0_CLK:
		camio_csi0_clk =
		clk = clk_get(NULL, "csi_clk");
		break;

	case CAMIO_CSI1_CLK:
		camio_csi1_clk =
		clk = clk_get(&camio_dev->dev, "csi_clk");
		break;

	case CAMIO_VFE_PCLK:
		camio_vfe_pclk =
		clk = clk_get(NULL, "vfe_pclk");
		break;

	case CAMIO_CSI0_PCLK:
		camio_csi0_pclk =
		clk = clk_get(NULL, "csi_pclk");
		break;

	case CAMIO_CSI1_PCLK:
		camio_csi1_pclk =
		clk = clk_get(&camio_dev->dev, "csi_pclk");
		break;

	case CAMIO_JPEG_CLK:
		camio_jpeg_clk =
		clk = clk_get(NULL, "ijpeg_clk");
		msm_camio_clk_rate_set_2(clk, 228571000);
		break;

	case CAMIO_JPEG_PCLK:
		camio_jpeg_pclk =
		clk = clk_get(NULL, "ijpeg_pclk");
		break;

	case CAMIO_VPE_CLK:
		camio_vpe_clk =
		clk = clk_get(NULL, "vpe_clk");
		vpe_clk_rate = clk_round_rate(camio_vpe_clk, 150000000);
		clk_set_rate(camio_vpe_clk, vpe_clk_rate);

		break;

	case CAMIO_VPE_PCLK:
		camio_vpe_pclk =
		clk = clk_get(NULL, "vpe_pclk");
		break;

	default:
		break;
	}

	if (clk != NULL && !IS_ERR(clk))
		clk_enable(clk);
	else
		rc = -1;
	return rc;
}

int msm_camio_clk_disable(enum msm_camio_clk_type clktype)
{
	int rc = 0;
	struct clk *clk = NULL;

	switch (clktype) {
	case CAMIO_CAM_MCLK_CLK:
		clk = camio_cam_clk;
		break;

	case CAMIO_VFE_CLK:
		clk = camio_vfe_clk;
		break;

	case CAMIO_CSI_SRC_CLK:
		clk = camio_csi_src_clk;
		break;

	case CAMIO_CSI0_VFE_CLK:
		clk = camio_csi0_vfe_clk;
		break;

	case CAMIO_CSI1_VFE_CLK:
		clk = camio_csi1_vfe_clk;
		break;

	case CAMIO_CSI0_CLK:
		clk = camio_csi0_clk;
		break;

	case CAMIO_CSI1_CLK:
		clk = camio_csi1_clk;
		break;

	case CAMIO_VFE_PCLK:
		clk = camio_vfe_pclk;
		break;

	case CAMIO_CSI0_PCLK:
		clk = camio_csi0_pclk;
		break;

	case CAMIO_CSI1_PCLK:
		clk = camio_csi1_pclk;
		break;

	case CAMIO_JPEG_CLK:
		clk = camio_jpeg_clk;
		break;

	case CAMIO_JPEG_PCLK:
		clk = camio_jpeg_pclk;
		break;

	case CAMIO_VPE_CLK:
		clk = camio_vpe_clk;
		break;

	case CAMIO_VPE_PCLK:
		clk = camio_vpe_pclk;
		break;

	default:
		break;
	}

	if (clk != NULL && !IS_ERR(clk)) {
		clk_disable(clk);
		clk_put(clk);
	} else
		rc = -1;

	return rc;
}

void msm_camio_clk_rate_set(int rate)
{
	struct clk *clk = camio_cam_clk;
	clk_set_rate(clk, rate);
}

void msm_camio_clk_rate_set_2(struct clk *clk, int rate)
{
	clk_set_rate(clk, rate);
}

#ifndef CONFIG_MACH_VERDI_LTE
static irqreturn_t msm_io_csi_irq(int irq_num, void *data)
{
	uint32_t irq, irq2;
	irq = msm_io_r(csibase + MIPI_INTERRUPT_STATUS);
	irq2 = msm_io_r(csibase + 0x5C);
/* HTC_START Max Sun 20110721 For debugging CSI error */
	if (atomic_read(&csi_irq_debug) == true &&
		(atomic_read(&csi_irq_debug_cnt) < csi_irq_debug_max_cnt)) {
		atomic_inc(&csi_irq_debug_cnt) ;
		pr_info("[CAM] %s debug count  = %d \n", __func__, atomic_read(&csi_irq_debug_cnt));
		pr_info("[CAM] %s irq  MIPI_INTERRUPT_STATUS = 0x%x\n", __func__, irq);
		pr_info("[CAM] %s irq2 MIPI_INTERRUPT_STATUS = 0x%x\n", __func__, irq2);
	}
/* HTC_END */
	CDBG("[CAM] %s MIPI_INTERRUPT_STATUS = 0x%x\n", __func__, irq);

	if (irq & MIPI_IMASK_ERROR_OCCUR) {
		if (irq & MIPI_IMASK_ERR_SOT)
			pr_info("[CAM]msm_io_csi_irq: SOT error\n");
		if (irq & MIPI_IMASK_ERR_SOT_SYNC)
			pr_info("[CAM]msm_io_csi_irq: SOT SYNC error\n");
		if (irq & MIPI_IMASK_CLK_CTL_ERROR)
			pr_info("[CAM]msm_io_csi_irq: Clock lane ULPM mode sequence or command error\n");
		if (irq & MIPI_IMASK_DATA_CTL_ERROR)
			pr_info("[CAM]msm_io_csi_irq: Data lane ULPM mode sequence or command error\n");
#if 0
		if (irq & MIPI_IMASK_CLK_CMM_ERROR) /* defeatured */
			pr_err("[CAM]msm_io_csi_irq: Common mode error detected by PHY CLK lane\n");
		if (irq & MIPI_IMASK_DATA_CMM_ERROR) /* defeatured */
			pr_err("[CAM]msm_io_csi_irq: Common mode error detected by PHY data lane\n");
#endif
		if (irq & MIPI_IMASK_DL0_SYNC_ERROR)
			pr_info("[CAM]msm_io_csi_irq: An error occured while synchronizing data " \
				"from PHY to VFE clock domain on data lane 0\n");
		if (irq & MIPI_IMASK_DL1_SYNC_ERROR)
			pr_info("[CAM]msm_io_csi_irq: An error occured while synchronizing data " \
				"from PHY to VFE clock domain on data lane 1\n");
		if (irq & MIPI_IMASK_DL2_SYNC_ERROR)
			pr_info("[CAM]msm_io_csi_irq: An error occured while synchronizing data " \
				"from PHY to VFE clock domain on data lane 2\n");
		if (irq & MIPI_IMASK_DL3_SYNC_ERROR)
			pr_info("[CAM]msm_io_csi_irq: An error occured while synchronizing data " \
				"from PHY to VFE clock domain on data lane 3\n");
		if (irq & MIPI_IMASK_ECC_ERROR)
			pr_info("[CAM]msm_io_csi_irq: ECC error\n");
		if (irq & MIPI_IMASK_CRC_ERROR)
			pr_info("[CAM]msm_io_csi_irq: CRC error\n");
		if (irq & MIPI_IMASK_FRAME_SYNC_ERROR)
			pr_info("[CAM]msm_io_csi_irq: FS not paired with FE\n");
/*		if (irq & MIPI_IMASK_ID_ERROR)
			pr_info("[CAM]msm_io_csi_irq: Long packet ID not defined\n");
*/
		if (irq & MIPI_IMASK_EOT_ERROR)
			pr_info("[CAM]msm_io_csi_irq: The received data is less than the value indicated by WC\n");
		if (irq & MIPI_IMASK_DL0_FIFO_OVERFLOW)
			pr_info("[CAM]msm_io_csi_irq: Data lane 0 FIFO overflow\n");
		if (irq & MIPI_IMASK_DL1_FIFO_OVERFLOW)
			pr_info("[CAM]msm_io_csi_irq: Data lane 1 FIFO overflow\n");
		if (irq & MIPI_IMASK_DL2_FIFO_OVERFLOW)
			pr_info("[CAM]msm_io_csi_irq: Data lane 2 FIFO overflow\n");
		if (irq & MIPI_IMASK_DL3_FIFO_OVERFLOW)
			pr_info("[CAM]msm_io_csi_irq: Data lane 3 FIFO overflow\n");
	}


	msm_io_w(irq, csibase + MIPI_INTERRUPT_STATUS);
	return IRQ_HANDLED;
}
#endif

int msm_camio_jpeg_clk_disable(void)
{
	int rc = 0;
	if (fs_ijpeg) {
		rc = regulator_disable(fs_ijpeg);
		if (rc < 0) {
			CDBG("[CAM] %s: Regulator disable failed %d\n", __func__, rc);
			return rc;
		}
		regulator_put(fs_ijpeg);
	}
	rc = msm_camio_clk_disable(CAMIO_JPEG_PCLK);
	if (rc < 0)
		return rc;
	rc = msm_camio_clk_disable(CAMIO_JPEG_CLK);
	CDBG("[CAM] %s: exit %d\n", __func__, rc);
	return rc;
}

int msm_camio_jpeg_clk_enable(void)
{
	int rc = 0;
	rc = msm_camio_clk_enable(CAMIO_JPEG_CLK);
	if (rc < 0)
		return rc;
	rc = msm_camio_clk_enable(CAMIO_JPEG_PCLK);
	if (rc < 0)
		return rc;
	fs_ijpeg = regulator_get(NULL, "fs_ijpeg");
	if (IS_ERR(fs_ijpeg)) {
		CDBG("[CAM] %s: Regulator FS_IJPEG get failed %ld\n", __func__,
			PTR_ERR(fs_ijpeg));
		fs_ijpeg = NULL;
	} else if (regulator_enable(fs_ijpeg)) {
		CDBG("[CAM] %s: Regulator FS_IJPEG enable failed\n", __func__);
		regulator_put(fs_ijpeg);
	}
	CDBG("[CAM] %s: exit %d\n", __func__, rc);
	return rc;
}

int msm_camio_vpe_clk_disable(void)
{
	int rc = 0;
	if (fs_vpe) {
		regulator_disable(fs_vpe);
		regulator_put(fs_vpe);
	}

	rc = msm_camio_clk_disable(CAMIO_VPE_CLK);
	if (rc < 0)
		return rc;
	rc = msm_camio_clk_disable(CAMIO_VPE_PCLK);
	return rc;
}

int msm_camio_vpe_clk_enable(void)
{
	int rc = 0;
	fs_vpe = regulator_get(NULL, "fs_vpe");
	if (IS_ERR(fs_vpe)) {
		CDBG("[CAM] %s: Regulator FS_VPE get failed %ld\n", __func__,
			PTR_ERR(fs_vpe));
		fs_vpe = NULL;
	} else if (regulator_enable(fs_vpe)) {
		CDBG("[CAM] %s: Regulator FS_VPE enable failed\n", __func__);
		regulator_put(fs_vpe);
	}

	rc = msm_camio_clk_enable(CAMIO_VPE_CLK);
	if (rc < 0)
		return rc;
	rc = msm_camio_clk_enable(CAMIO_VPE_PCLK);
	return rc;
}

int msm_camio_enable(struct platform_device *pdev)
{
	int rc = 0;
	struct msm_camera_sensor_info *sinfo = pdev->dev.platform_data;
	struct msm_camera_device_platform_data *camdev = sinfo->pdata;
	uint32_t val;

	camio_dev = pdev;
	camio_ext = camdev->ioext;
	camio_clk = camdev->ioclk;

	msm_camio_clk_enable(CAMIO_VFE_CLK);
	msm_camio_clk_enable(CAMIO_CSI0_VFE_CLK);
	msm_camio_clk_enable(CAMIO_CSI1_VFE_CLK);
	msm_camio_clk_enable(CAMIO_CSI_SRC_CLK);
	msm_camio_clk_enable(CAMIO_CSI0_CLK);
	msm_camio_clk_enable(CAMIO_CSI1_CLK);
	msm_camio_clk_enable(CAMIO_VFE_PCLK);
	msm_camio_clk_enable(CAMIO_CSI0_PCLK);
	msm_camio_clk_enable(CAMIO_CSI1_PCLK);

	csiio = request_mem_region(camio_ext.csiphy,
		camio_ext.csisz, pdev->name);
	if (!csiio) {
		rc = -EBUSY;
		goto common_fail;
	}

	csibase = ioremap(camio_ext.csiphy,
		camio_ext.csisz);
	if (!csibase) {
		rc = -ENOMEM;
		goto csi_busy;
	}
#ifndef CONFIG_MACH_VERDI_LTE
	rc = request_irq(camio_ext.csiirq, msm_io_csi_irq,
		IRQF_TRIGGER_HIGH, "csi", 0);
	if (rc < 0)
		goto csi_irq_fail;
#endif
	msleep(10);
	val = (settle_cnt<<
		MIPI_PHY_D0_CONTROL2_SETTLE_COUNT_SHFT) |
		(0x0F << MIPI_PHY_D0_CONTROL2_HS_TERM_IMP_SHFT) |
		(0x0 << MIPI_PHY_D0_CONTROL2_LP_REC_EN_SHFT) |
		(0x1 << MIPI_PHY_D0_CONTROL2_ERR_SOT_HS_EN_SHFT);
	CDBG("[CAM] %s MIPI_PHY_D0_CONTROL2 val=0x%x\n", __func__, val);
	msm_io_w(val, csibase + MIPI_PHY_D0_CONTROL2);
	msm_io_w(val, csibase + MIPI_PHY_D1_CONTROL2);
	msm_io_w(val, csibase + MIPI_PHY_D2_CONTROL2);
	msm_io_w(val, csibase + MIPI_PHY_D3_CONTROL2);

	val = (0x0F << MIPI_PHY_CL_CONTROL_HS_TERM_IMP_SHFT) |
		(0x0 << MIPI_PHY_CL_CONTROL_LP_REC_EN_SHFT);
	CDBG("[CAM] %s MIPI_PHY_CL_CONTROL val=0x%x\n", __func__, val);
	msm_io_w(val, csibase + MIPI_PHY_CL_CONTROL);
	return 0;

#ifndef CONFIG_MACH_VERDI_LTE
csi_irq_fail:
	iounmap(csibase);
#endif
csi_busy:
	release_mem_region(camio_ext.csiphy, camio_ext.csisz);
common_fail:
	msm_camio_clk_disable(CAMIO_CAM_MCLK_CLK);
	msm_camio_clk_disable(CAMIO_CSI0_VFE_CLK);
	msm_camio_clk_disable(CAMIO_CSI0_CLK);
	msm_camio_clk_disable(CAMIO_CSI1_VFE_CLK);
	msm_camio_clk_disable(CAMIO_CSI1_CLK);
	msm_camio_clk_disable(CAMIO_VFE_PCLK);
	msm_camio_clk_disable(CAMIO_CSI0_PCLK);
	msm_camio_clk_disable(CAMIO_CSI1_PCLK);
	msm_camera_fs_disable();
	return rc;
}

/*HTC_START Horng 20110905*/
void msm_mipi_csi_disable(void)
{
	uint32_t val;

#ifndef CONFIG_MACH_VERDI_LTE
	/*clear IRQ bits referred from csi_config */
	/*msm_io_w(0xFFF7F3FF, csibase + MIPI_INTERRUPT_STATUS);*/
	free_irq(camio_ext.csiirq, 0);
#endif

	val = (settle_cnt <<
		MIPI_PHY_D0_CONTROL2_SETTLE_COUNT_SHFT) |
		(0x0F << MIPI_PHY_D0_CONTROL2_HS_TERM_IMP_SHFT) |
		(0x0 << MIPI_PHY_D0_CONTROL2_LP_REC_EN_SHFT) |
		(0x1 << MIPI_PHY_D0_CONTROL2_ERR_SOT_HS_EN_SHFT);
	CDBG("[CAM] %s MIPI_PHY_D0_CONTROL2 val=0x%x\n", __func__, val);
	msm_io_w(val, csibase + MIPI_PHY_D0_CONTROL2);
	msm_io_w(val, csibase + MIPI_PHY_D1_CONTROL2);
	msm_io_w(val, csibase + MIPI_PHY_D2_CONTROL2);
	msm_io_w(val, csibase + MIPI_PHY_D3_CONTROL2);

	val = (0x0F << MIPI_PHY_CL_CONTROL_HS_TERM_IMP_SHFT) |
		(0x0 << MIPI_PHY_CL_CONTROL_LP_REC_EN_SHFT);
	CDBG("[CAM] %s MIPI_PHY_CL_CONTROL val=0x%x\n", __func__, val);
	msm_io_w(val, csibase + MIPI_PHY_CL_CONTROL);
	msleep(10);


	val = msm_io_r(csibase + MIPI_PHY_D1_CONTROL);
	val &= ~((0x1 << MIPI_PHY_D1_CONTROL_MIPI_CLK_PHY_SHUTDOWNB_SHFT) |
		(0x1 << MIPI_PHY_D1_CONTROL_MIPI_DATA_PHY_SHUTDOWNB_SHFT));
	CDBG("[CAM] %s MIPI_PHY_D1_CONTROL val=0x%x\n", __func__, val);
	msm_io_w(val, csibase + MIPI_PHY_D1_CONTROL);
	usleep_range(5000, 6000);


	iounmap(csibase);
	release_mem_region(camio_ext.csiphy, camio_ext.csisz);
}
/*HTC_END*/

void msm_camio_disable(struct platform_device *pdev)
{
/*HTC_START Horng 20110905*/
/*Disable MIPI CALIBRATION CONTROL and close MIPI sequence changed*/
#if 0
	uint32_t val;
	val = (0x0 << MIPI_CALIBRATION_CONTROL_SWCAL_CAL_EN_SHFT) |
		(0x0 <<
		MIPI_CALIBRATION_CONTROL_SWCAL_STRENGTH_OVERRIDE_EN_SHFT) |
		(0x0 << MIPI_CALIBRATION_CONTROL_CAL_SW_HW_MODE_SHFT) |
		(0x0 << MIPI_CALIBRATION_CONTROL_MANUAL_OVERRIDE_EN_SHFT);
	CDBG("[CAM] %s MIPI_CALIBRATION_CONTROL val=0x%x\n", __func__, val);
	msm_io_w(val, csibase + MIPI_CALIBRATION_CONTROL);
#endif

#if 0
	val = (settle_cnt <<
		MIPI_PHY_D0_CONTROL2_SETTLE_COUNT_SHFT) |
		(0x0F << MIPI_PHY_D0_CONTROL2_HS_TERM_IMP_SHFT) |
		(0x0 << MIPI_PHY_D0_CONTROL2_LP_REC_EN_SHFT) |
		(0x1 << MIPI_PHY_D0_CONTROL2_ERR_SOT_HS_EN_SHFT);
	CDBG("[CAM] %s MIPI_PHY_D0_CONTROL2 val=0x%x\n", __func__, val);
	msm_io_w(val, csibase + MIPI_PHY_D0_CONTROL2);
	msm_io_w(val, csibase + MIPI_PHY_D1_CONTROL2);
	msm_io_w(val, csibase + MIPI_PHY_D2_CONTROL2);
	msm_io_w(val, csibase + MIPI_PHY_D3_CONTROL2);

	val = (0x0F << MIPI_PHY_CL_CONTROL_HS_TERM_IMP_SHFT) |
		(0x0 << MIPI_PHY_CL_CONTROL_LP_REC_EN_SHFT);
	CDBG("[CAM] %s MIPI_PHY_CL_CONTROL val=0x%x\n", __func__, val);
	msm_io_w(val, csibase + MIPI_PHY_CL_CONTROL);
	msleep(10);


	val = msm_io_r(csibase + MIPI_PHY_D1_CONTROL);
	val &= ~((0x1 << MIPI_PHY_D1_CONTROL_MIPI_CLK_PHY_SHUTDOWNB_SHFT) |
		(0x1 << MIPI_PHY_D1_CONTROL_MIPI_DATA_PHY_SHUTDOWNB_SHFT));
	CDBG("[CAM] %s MIPI_PHY_D1_CONTROL val=0x%x\n", __func__, val);
	msm_io_w(val, csibase + MIPI_PHY_D1_CONTROL);
	usleep_range(5000, 6000);

#ifndef CONFIG_MACH_VERDI_LTE
	/*clear IRQ bits referred from csi_config */
	/*msm_io_w(0xFFF7F3FF, csibase + MIPI_INTERRUPT_STATUS);*/
	free_irq(camio_ext.csiirq, 0);
#endif
	iounmap(csibase);
	release_mem_region(camio_ext.csiphy, camio_ext.csisz);
#endif
/*HTC_END*/

	CDBG("[CAM] disable clocks\n");

	msm_camio_clk_disable(CAMIO_CSI0_VFE_CLK);
	msm_camio_clk_disable(CAMIO_CSI0_CLK);
	msm_camio_clk_disable(CAMIO_CSI1_VFE_CLK);
	msm_camio_clk_disable(CAMIO_CSI1_CLK);
	msm_camio_clk_disable(CAMIO_VFE_PCLK);
	msm_camio_clk_disable(CAMIO_CSI0_PCLK);
	msm_camio_clk_disable(CAMIO_CSI1_PCLK);
	msm_camio_clk_disable(CAMIO_CSI_SRC_CLK);
	msm_camio_clk_disable(CAMIO_VFE_CLK);
}


int msm_camio_sensor_clk_on(struct platform_device *pdev)
{
	struct msm_camera_sensor_info *sinfo = pdev->dev.platform_data;
	struct msm_camera_device_platform_data *camdev = sinfo->pdata;
	camio_dev = pdev;
	camio_ext = camdev->ioext;
	camio_clk = camdev->ioclk;
	msm_camera_fs_enable();
	msleep(10);
	return msm_camio_clk_enable(CAMIO_CAM_MCLK_CLK);
}

int msm_camio_sensor_clk_off(struct platform_device *pdev)
{
	msm_camera_fs_disable();
	return msm_camio_clk_disable(CAMIO_CAM_MCLK_CLK);

}

void msm_camio_vfe_blk_reset(void)
{
	return;
}

int msm_camio_probe_on(struct platform_device *pdev)
{
	struct msm_camera_sensor_info *sinfo = pdev->dev.platform_data;
	struct msm_camera_device_platform_data *camdev = sinfo->pdata;
	camio_dev = pdev;
	camio_ext = camdev->ioext;
	camio_clk = camdev->ioclk;
	return msm_camio_clk_enable(CAMIO_CAM_MCLK_CLK);
}

int msm_camio_probe_off(struct platform_device *pdev)
{
	msm_camera_fs_disable();
	return msm_camio_clk_disable(CAMIO_CAM_MCLK_CLK);
}

int msm_camio_csi_config_withReceiverDisabled(struct msm_camera_csi_params *csi_params)
{
	int rc = 0;
	uint32_t val = 0;
/* HTC_START Max Sun 20110721 For debugging CSI error */
	msm_camio_enable_csi_log();
/* HTC_END */
	CDBG("[CAM] msm_camio_csi_config \n");

	if (csi_params->settle_cnt)
		settle_cnt = csi_params->settle_cnt;
	else
		settle_cnt = 0x14;

	if (csi_params->hs_impedence)
		hs_impedence_shift = csi_params->hs_impedence;
	else
		hs_impedence_shift = DEFAULT_HS_IMPEDENCE;

	if (csi_params->mipi_driving_strength)
		mipi_driving_strength_shift = csi_params->mipi_driving_strength;
	else
		mipi_driving_strength_shift = DEFAULT_MIPI_DRIVING_STRENGTH;

	/* SOT_ECC_EN enable error correction for SYNC (data-lane) */
	msm_io_w(0x4, csibase + MIPI_PHY_CONTROL);

	/* SW_RST to the CSI core */
	msm_io_w(MIPI_PROTOCOL_CONTROL_SW_RST_BMSK,
		csibase + MIPI_PROTOCOL_CONTROL);

	/* PROTOCOL CONTROL */
	val = MIPI_PROTOCOL_CONTROL_LONG_PACKET_HEADER_CAPTURE_BMSK |
		MIPI_PROTOCOL_CONTROL_DECODE_ID_BMSK |
		MIPI_PROTOCOL_CONTROL_ECC_EN_BMSK;
	val |= (uint32_t)(csi_params->data_format) <<
		MIPI_PROTOCOL_CONTROL_DATA_FORMAT_SHFT;
	val |= csi_params->dpcm_scheme <<
		MIPI_PROTOCOL_CONTROL_DPCM_SCHEME_SHFT;
	CDBG("[CAM] %s MIPI_PROTOCOL_CONTROL val=0x%x\n", __func__, val);
	msm_io_w(val, csibase + MIPI_PROTOCOL_CONTROL);

/*HTC_START Horng 20110905*/
/*Disable MIPI CALIBRATION CONTROL*/
#if 0
	/* SW CAL EN */
	val = (0x1 << MIPI_CALIBRATION_CONTROL_SWCAL_CAL_EN_SHFT) |
		(0x1 <<
		MIPI_CALIBRATION_CONTROL_SWCAL_STRENGTH_OVERRIDE_EN_SHFT) |
		(0x1 << MIPI_CALIBRATION_CONTROL_CAL_SW_HW_MODE_SHFT) |
		(0x1 << MIPI_CALIBRATION_CONTROL_MANUAL_OVERRIDE_EN_SHFT);
	CDBG("[CAM] %s MIPI_CALIBRATION_CONTROL val=0x%x\n", __func__, val);
	msm_io_w(val, csibase + MIPI_CALIBRATION_CONTROL);
#endif
/*HTC_END*/

	/* settle_cnt is very sensitive to speed!
	increase this value to run at higher speeds */
	val = (settle_cnt <<
		MIPI_PHY_D0_CONTROL2_SETTLE_COUNT_SHFT) |
		(hs_impedence_shift << MIPI_PHY_D0_CONTROL2_HS_TERM_IMP_SHFT) |
		(0x1 << MIPI_PHY_D0_CONTROL2_ERR_SOT_HS_EN_SHFT);
	CDBG("[CAM] %s MIPI_PHY_D0_CONTROL2 val=0x%x\n", __func__, val);
	msm_io_w(val, csibase + MIPI_PHY_D0_CONTROL2);
	msm_io_w(val, csibase + MIPI_PHY_D1_CONTROL2);
	msm_io_w(val, csibase + MIPI_PHY_D2_CONTROL2);
	msm_io_w(val, csibase + MIPI_PHY_D3_CONTROL2);


	val = (0x0F << MIPI_PHY_CL_CONTROL_HS_TERM_IMP_SHFT) |
		(0x1 << MIPI_PHY_CL_CONTROL_LP_REC_EN_SHFT);
	CDBG("[CAM] %s MIPI_PHY_CL_CONTROL val=0x%x\n", __func__, val);
	msm_io_w(val, csibase + MIPI_PHY_CL_CONTROL);

	/* 0306 QCT Hody: for mipi CRC error */
	/* val = 0 << MIPI_PHY_D0_CONTROL_HS_REC_EQ_SHFT; */
	val = csi_params->mipi_driving_strength << MIPI_PHY_D0_CONTROL_HS_REC_EQ_SHFT;
	msm_io_w(val, csibase + MIPI_PHY_D0_CONTROL);

	val = (0x1 << MIPI_PHY_D1_CONTROL_MIPI_CLK_PHY_SHUTDOWNB_SHFT) |
		(0x1 << MIPI_PHY_D1_CONTROL_MIPI_DATA_PHY_SHUTDOWNB_SHFT);
	CDBG("[CAM] %s MIPI_PHY_D1_CONTROL val=0x%x\n", __func__, val);
	msm_io_w(val, csibase + MIPI_PHY_D1_CONTROL);

	msm_io_w(0x00000000, csibase + MIPI_PHY_D2_CONTROL);
	msm_io_w(0x00000000, csibase + MIPI_PHY_D3_CONTROL);

	/* halcyon only supports 1 or 2 lane */
	switch (csi_params->lane_cnt) {
	case 1:
		msm_io_w(csi_params->lane_assign << 8 | 0x4,
			csibase + MIPI_CAMERA_CNTL);
		break;
	case 2:
		msm_io_w(csi_params->lane_assign << 8 | 0x5,
			csibase + MIPI_CAMERA_CNTL);
		break;
	case 3:
		msm_io_w(csi_params->lane_assign << 8 | 0x6,
			csibase + MIPI_CAMERA_CNTL);
		break;
	case 4:
		msm_io_w(csi_params->lane_assign << 8 | 0x7,
			csibase + MIPI_CAMERA_CNTL);
		break;
	}

	/* mask out ID_ERROR[19], DATA_CMM_ERR[11]
	and CLK_CMM_ERR[10] - de-featured */
	msm_io_w(0xFFF7F3FF, csibase + MIPI_INTERRUPT_MASK);
	/*clear IRQ bits*/
	msm_io_w(0xFFF7F3FF, csibase + MIPI_INTERRUPT_STATUS);

	return rc;
}
/* HTC_START Max Sun 20110721 For debugging CSI error */
void msm_camio_enable_csi_log(void)
{
	pr_info("[CAM] msm_camio_enable_csi_log");
	/* Enable debug flag and reset the debug log count */
	atomic_set(&csi_irq_debug, 1);
	atomic_set(&csi_irq_debug_cnt, 0);
}

void msm_camio_disable_csi_log(void)
{
	pr_info("[CAM] msm_camio_disable_csi_log");
	/* Disble debug flag and reset the debug log count */
	atomic_set(&csi_irq_debug, 0);
	atomic_set(&csi_irq_debug_cnt, 0);
}
/* HTC_END */
int msm_camio_csi_config(struct msm_camera_csi_params *csi_params)
{
	int rc = 0;
	uint32_t val = 0;

	CDBG("[CAM] msm_camio_csi_config \n");
/* HTC_START Max Sun 20110721 Enable debug flag and reset the debug log count */
	msm_camio_enable_csi_log();
/* HTC_END */
	if (csi_params->settle_cnt)
		settle_cnt = csi_params->settle_cnt;
	else
		settle_cnt = 0x14;

	if (csi_params->hs_impedence)
		hs_impedence_shift = csi_params->hs_impedence;
	else
		hs_impedence_shift = DEFAULT_HS_IMPEDENCE;

	if (csi_params->mipi_driving_strength)
		mipi_driving_strength_shift = csi_params->mipi_driving_strength;
	else
		mipi_driving_strength_shift = DEFAULT_MIPI_DRIVING_STRENGTH;

	/* SOT_ECC_EN enable error correction for SYNC (data-lane) */
	msm_io_w(0x4, csibase + MIPI_PHY_CONTROL);

	/* SW_RST to the CSI core */
	msm_io_w(MIPI_PROTOCOL_CONTROL_SW_RST_BMSK,
		csibase + MIPI_PROTOCOL_CONTROL);

	/* PROTOCOL CONTROL */
	val = MIPI_PROTOCOL_CONTROL_LONG_PACKET_HEADER_CAPTURE_BMSK |
		MIPI_PROTOCOL_CONTROL_DECODE_ID_BMSK |
		MIPI_PROTOCOL_CONTROL_ECC_EN_BMSK;
	val |= (uint32_t)(csi_params->data_format) <<
		MIPI_PROTOCOL_CONTROL_DATA_FORMAT_SHFT;
	val |= csi_params->dpcm_scheme <<
		MIPI_PROTOCOL_CONTROL_DPCM_SCHEME_SHFT;
	CDBG("[CAM] %s MIPI_PROTOCOL_CONTROL val=0x%x\n", __func__, val);
	msm_io_w(val, csibase + MIPI_PROTOCOL_CONTROL);

/*HTC_START Horng 20110905*/
/*Disable MIPI CALIBRATION CONTROL*/
#if 0
	/* SW CAL EN */
	val = (0x1 << MIPI_CALIBRATION_CONTROL_SWCAL_CAL_EN_SHFT) |
		(0x1 <<
		MIPI_CALIBRATION_CONTROL_SWCAL_STRENGTH_OVERRIDE_EN_SHFT) |
		(0x1 << MIPI_CALIBRATION_CONTROL_CAL_SW_HW_MODE_SHFT) |
		(0x1 << MIPI_CALIBRATION_CONTROL_MANUAL_OVERRIDE_EN_SHFT);
	CDBG("[CAM] %s MIPI_CALIBRATION_CONTROL val=0x%x\n", __func__, val);
	msm_io_w(val, csibase + MIPI_CALIBRATION_CONTROL);
#endif
/*HTC_END*/

	/* settle_cnt is very sensitive to speed!
	increase this value to run at higher speeds */
	val = (settle_cnt <<
		MIPI_PHY_D0_CONTROL2_SETTLE_COUNT_SHFT) |
		(hs_impedence_shift << MIPI_PHY_D0_CONTROL2_HS_TERM_IMP_SHFT) |
		(0x1 << MIPI_PHY_D0_CONTROL2_LP_REC_EN_SHFT) |
		(0x1 << MIPI_PHY_D0_CONTROL2_ERR_SOT_HS_EN_SHFT);
	CDBG("[CAM] %s MIPI_PHY_D0_CONTROL2 val=0x%x\n", __func__, val);
	msm_io_w(val, csibase + MIPI_PHY_D0_CONTROL2);
	msm_io_w(val, csibase + MIPI_PHY_D1_CONTROL2);
/*HTC_START Horng 20110802*/
/*Disable MIPI lane2,lane3 if not in use*/
	if (csi_params->lane_cnt > 2) {
		msm_io_w(val, csibase + MIPI_PHY_D2_CONTROL2);
		msm_io_w(val, csibase + MIPI_PHY_D3_CONTROL2);
	} else {
		msm_io_w(0x00000000, csibase + MIPI_PHY_D2_CONTROL2);
		msm_io_w(0x00000000, csibase + MIPI_PHY_D3_CONTROL2);
	}
/*HTC_END*/

	val = (0x0F << MIPI_PHY_CL_CONTROL_HS_TERM_IMP_SHFT) |
		(0x1 << MIPI_PHY_CL_CONTROL_LP_REC_EN_SHFT);
	CDBG("[CAM] %s MIPI_PHY_CL_CONTROL val=0x%x\n", __func__, val);
	msm_io_w(val, csibase + MIPI_PHY_CL_CONTROL);

	/* 0306 QCT Hody: for mipi CRC error */
	val = mipi_driving_strength_shift << MIPI_PHY_D0_CONTROL_HS_REC_EQ_SHFT;
	msm_io_w(val, csibase + MIPI_PHY_D0_CONTROL);

	val = (0x1 << MIPI_PHY_D1_CONTROL_MIPI_CLK_PHY_SHUTDOWNB_SHFT) |
		(0x1 << MIPI_PHY_D1_CONTROL_MIPI_DATA_PHY_SHUTDOWNB_SHFT);
	CDBG("[CAM] %s MIPI_PHY_D1_CONTROL val=0x%x\n", __func__, val);
	msm_io_w(val, csibase + MIPI_PHY_D1_CONTROL);

	msm_io_w(0x00000000, csibase + MIPI_PHY_D2_CONTROL);
	msm_io_w(0x00000000, csibase + MIPI_PHY_D3_CONTROL);

	/* halcyon only supports 1 or 2 lane */
	switch (csi_params->lane_cnt) {
	case 1:
		msm_io_w(csi_params->lane_assign << 8 | 0x4,
			csibase + MIPI_CAMERA_CNTL);
		break;
	case 2:
		msm_io_w(csi_params->lane_assign << 8 | 0x5,
			csibase + MIPI_CAMERA_CNTL);
		break;
	case 3:
		msm_io_w(csi_params->lane_assign << 8 | 0x6,
			csibase + MIPI_CAMERA_CNTL);
		break;
	case 4:
		msm_io_w(csi_params->lane_assign << 8 | 0x7,
			csibase + MIPI_CAMERA_CNTL);
		break;
	}

	/* mask out ID_ERROR[19], DATA_CMM_ERR[11]
	and CLK_CMM_ERR[10] - de-featured */
/* HTC_START Max Sun 20110721 For debugging CSI error, change the IRQ mask to get the infomative IRQ */
	msm_io_w(0xfffff3c0, csibase + MIPI_INTERRUPT_MASK);
	/*clear IRQ bits*/
	msm_io_w(0xfffff3c0, csibase + MIPI_INTERRUPT_STATUS);
/* HTC_END */

	return rc;
}

void msm_camio_set_perf_lvl(enum msm_bus_perf_setting perf_setting)
{
	static uint32_t bus_perf_client;
	int rc = 0;
	switch (perf_setting) {
	case S_INIT:
		bus_perf_client =
			msm_bus_scale_register_client(&cam_bus_client_pdata);
		if (!bus_perf_client) {
			pr_err("[CAM] %s: Registration Failed!!!\n", __func__);
			bus_perf_client = 0;
			return;
		}
		pr_info("[CAM] %s: S_INIT rc = %u\n", __func__, bus_perf_client);
		break;
	case S_EXIT:
		if (bus_perf_client) {
			pr_info("[CAM] %s: S_EXIT\n", __func__);
			msm_bus_scale_unregister_client(bus_perf_client);
		} else
			pr_err("[CAM] %s: Bus Client NOT Registered!!!\n", __func__);
		break;
	case S_PREVIEW:
		if (bus_perf_client) {
			rc = msm_bus_scale_client_update_request(
				bus_perf_client, 1);
			pr_info("[CAM] %s: S_PREVIEW rc = %d\n", __func__, rc);
		} else
			pr_err("[CAM] %s: Bus Client NOT Registered!!!\n", __func__);
		break;
	case S_VIDEO:
		if (bus_perf_client) {
			rc = msm_bus_scale_client_update_request(
				bus_perf_client, 2);
			pr_info("[CAM] %s: S_VIDEO rc = %d\n", __func__, rc);
		} else
			pr_err("[CAM] %s: Bus Client NOT Registered!!!\n", __func__);
		break;
	case S_CAPTURE:
		if (bus_perf_client) {
			rc = msm_bus_scale_client_update_request(
				bus_perf_client, 3);
			pr_info("[CAM] %s: S_CAPTURE rc = %d\n", __func__, rc);
		} else
			pr_err("[CAM] %s: Bus Client NOT Registered!!!\n", __func__);
		break;

	case S_ZSL:
		if (bus_perf_client) {
			rc = msm_bus_scale_client_update_request(
				bus_perf_client, 4);
			pr_info("[CAM] %s: S_ZSL rc = %d\n", __func__, rc);
		} else
			pr_err("[CAM] %s: Bus Client NOT Registered!!!\n", __func__);
		break;
	case S_STEREO_VIDEO:
		if (bus_perf_client) {
			rc = msm_bus_scale_client_update_request(
				bus_perf_client, 5);
			pr_info("[CAM] %s: S_STEREO_VIDEO rc = %d\n", __func__, rc);
		} else
			pr_err("[CAM] %s: Bus Client NOT Registered!!!\n", __func__);
		break;
	case S_STEREO_CAPTURE:
		if (bus_perf_client) {
			rc = msm_bus_scale_client_update_request(
				bus_perf_client, 6);
			pr_info("[CAM] %s: S_STEREO_VIDEO rc = %d\n", __func__, rc);
		} else
			pr_err("[CAM] %s: Bus Client NOT Registered!!!\n", __func__);
		break;
	case S_DEFAULT:
		break;
	default:
		pr_info("[CAM] %s: INVALID CASE\n", __func__);
	}
}

/* Dummy function for build pass */
void msm_camio_camif_pad_reg_reset(void)
{
}

void msm_camio_csi_misr_debug_on(void)
{
    msm_io_w(0x1F16, csibase + 0x28);
}

void msm_camio_csi_misr_read(void)
{
	pr_info("[CAM] %s: %d\n", __func__, msm_io_r(csibase + 0x60));
}


void msm_camio_csi_core_reset(void)
{
    uint32_t val;
    val = msm_io_r(MSM_MMSS_CLK_CTL_BASE + 0x210);
    msm_io_w(val | 0x1000100, MSM_MMSS_CLK_CTL_BASE + 0x210);
#if 0
    val = msm_io_r(clkctl + 0x20C);
    msm_io_w(val | 0x20000, clkctl + 0x20C);
#endif
}

void msm_camio_csi_core_soft_reset(void)
{
    msm_io_w(MIPI_PROTOCOL_CONTROL_SW_RST_BMSK,
    csibase + MIPI_PROTOCOL_CONTROL);
}

void msm_camio_csi_core_on(void)
{
    uint32_t val;
    val = msm_io_r(MSM_MMSS_CLK_CTL_BASE  + 0x210);
    msm_io_w(val & ~0x1000100, MSM_MMSS_CLK_CTL_BASE + 0x210);
#if 0
    val = msm_io_r(clkctl + 0x20C);
    msm_io_w(val & ~0x20000, clkctl + 0x20C);
#endif

}

int msm_camio_csi_disable_lp_rec(void)
{
    int rc = 0;
    uint32_t val = 0;
    pr_info("[CAM] msm_camio_csi_disable_lp_rec");
    val = (settle_cnt <<
		MIPI_PHY_D0_CONTROL2_SETTLE_COUNT_SHFT) |
		(hs_impedence_shift << MIPI_PHY_D0_CONTROL2_HS_TERM_IMP_SHFT) |
		(0x0 << MIPI_PHY_D0_CONTROL2_LP_REC_EN_SHFT) |
		(0x1 << MIPI_PHY_D0_CONTROL2_ERR_SOT_HS_EN_SHFT);
    CDBG("[CAM] %s MIPI_PHY_D0_CONTROL2 val=0x%x\n", __func__, val);
    msm_io_w(val, csibase + MIPI_PHY_D0_CONTROL2);
    msm_io_w(val, csibase + MIPI_PHY_D1_CONTROL2);
    msm_io_w(val, csibase + MIPI_PHY_D2_CONTROL2);
    msm_io_w(val, csibase + MIPI_PHY_D3_CONTROL2);

    val = (0x0F << MIPI_PHY_CL_CONTROL_HS_TERM_IMP_SHFT) |
		(0x1 << MIPI_PHY_CL_CONTROL_LP_REC_EN_SHFT);
    CDBG("[CAM] %s MIPI_PHY_CL_CONTROL val=0x%x\n", __func__, val);
    msm_io_w(val, csibase + MIPI_PHY_CL_CONTROL);
    return rc;
}


int msm_camio_csi_enable_lp_rec(void)
{
    int rc = 0;
    uint32_t val = 0;
    pr_info("[CAM] msm_camio_csi_enable_lp_rec");
    val = (settle_cnt <<
		MIPI_PHY_D0_CONTROL2_SETTLE_COUNT_SHFT) |
		(hs_impedence_shift << MIPI_PHY_D0_CONTROL2_HS_TERM_IMP_SHFT) |
		(0x1 << MIPI_PHY_D0_CONTROL2_LP_REC_EN_SHFT) |
		(0x1 << MIPI_PHY_D0_CONTROL2_ERR_SOT_HS_EN_SHFT);
    CDBG("[CAM] %s MIPI_PHY_D0_CONTROL2 val=0x%x\n", __func__, val);
    msm_io_w(val, csibase + MIPI_PHY_D0_CONTROL2);
    msm_io_w(val, csibase + MIPI_PHY_D1_CONTROL2);
    msm_io_w(val, csibase + MIPI_PHY_D2_CONTROL2);
    msm_io_w(val, csibase + MIPI_PHY_D3_CONTROL2);

    val = (0x0F << MIPI_PHY_CL_CONTROL_HS_TERM_IMP_SHFT) |
		(0x1 << MIPI_PHY_CL_CONTROL_LP_REC_EN_SHFT);
	CDBG("[CAM] %s MIPI_PHY_CL_CONTROL val=0x%x\n", __func__, val);
	msm_io_w(val, csibase + MIPI_PHY_CL_CONTROL);
    return rc;
}
