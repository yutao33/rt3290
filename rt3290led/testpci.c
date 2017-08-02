#include <linux/init.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/pm.h>
#include <linux/ioport.h>
#include <linux/leds.h>
#include <linux/interrupt.h>
#include <net/bluetooth/bluetooth.h>
#include <net/bluetooth/hci_core.h>

#include "testpci.h"

#define DRV_NAME "rtbt_example"
#define BT_FUN_CTRL 0x3c0

static DEFINE_PCI_DEVICE_TABLE(rt_pci_tbl) = {
  { PCI_DEVICE(0x1814, 0x3298) },
  //{ PCI_DEVICE(0x10ec, 0x8168) },
  {0, }
};

MODULE_DEVICE_TABLE(pci,rt_pci_tbl);

static int  rtbt_pci_probe  (struct pci_dev *, const struct pci_device_id *);
static void rtbt_pci_remove (struct pci_dev *);
static int  rtbt_pci_suspend(struct pci_dev *, pm_message_t state);
static int  rtbt_pci_resume (struct pci_dev *);
static void led_cdev_brightness_set(struct led_classdev *cdev, enum led_brightness value);

static struct pci_driver rt_bt_pci_driver = {
    .name = DRV_NAME,
    .id_table = rt_pci_tbl,
    .probe = rtbt_pci_probe,
    .remove = rtbt_pci_remove,
#ifdef CONFIG_PM
    .suspend = rtbt_pci_suspend,
    .resume = rtbt_pci_resume,
#endif
};

static struct hci_dev *hdev = NULL;
static struct pci_dev_privdata *pdata = NULL;
static struct led_classdev led_dev = { DRV_NAME, LED_OFF, 255, 0, led_cdev_brightness_set};
static void __iomem *csr_addr = NULL;

static void setLedPower(void __iomem *base_addr, bool enabled)
{
  unsigned int reg_word;
  
  reg_word = ioread32(base_addr + BT_FUN_CTRL);
  //printk("Readed register word: 0x%x\n",reg_word);
  if(enabled)
  {
    reg_word &= ~0x01000000; /* GPIO1 output enable: Output 0, input 1 	*/
    reg_word |=  0x00010000; /* GPIO1 output data */
  }else{
    reg_word &= ~0x00010000; /* GPIO1 output data */
  }
  iowrite32(reg_word,base_addr + BT_FUN_CTRL);
  mmiowb();
  //reg_word = ioread32(base_addr + BT_FUN_CTRL);
  //printk("Readed register word: 0x%x\n",reg_word);
}

static void led_cdev_brightness_set(struct led_classdev *cdev, enum led_brightness value)
{
  if(value != LED_OFF)
    setLedPower(csr_addr,true);
  else
    setLedPower(csr_addr,false);
}

static int __init example_init(void)
{
    int i;
    DEFINE_SEMAPHORE(init_sem);
    
    down(&init_sem);
      i = pci_register_driver(&rt_bt_pci_driver);
    up(&init_sem);
    
    printk(KERN_INFO "register_driver [%s]: %d\n",rt_bt_pci_driver.name,i);

    return i;
}

static void __exit example_exit(void)
{
  DEFINE_SEMAPHORE(exit_sem);
  
  printk("Example exit\n");
  down(&exit_sem);
    pci_unregister_driver(&rt_bt_pci_driver);
  up(&exit_sem);
}

static int rtbt_pci_probe(struct pci_dev *pdev, const struct pci_device_id *pent)
{
  DEFINE_SEMAPHORE(probe_sem);
  int ret = 0;
  unsigned long base_flags;
  int i = 0;
  int en;
  int led;
  unsigned int reg_word;
  
  
  printk(KERN_INFO "rtbt_pci_probe\n");
  
  down(&probe_sem);
  pdata = kzalloc(sizeof(*pdata),GFP_KERNEL);
  up(&probe_sem);
  
  if(!pdata)
  {
    printk(KERN_ERR DRV_NAME ": failed allocate resource for register data\n");
    ret = -ENOMEM;
    goto exit_l;
  }
  
  en = pci_enable_device(pdev);
  if(en)
  {
    printk(KERN_ERR "Error enabling pci device\n");
    ret = -ENODEV;
    goto exit_l;
  }
  
   base_flags = pci_resource_flags(pdev,i);
   if(!(base_flags & IORESOURCE_MEM))
   {
     printk(KERN_INFO "This region is NOT IORESOURCE_MEM\n");
     ret = -ENODEV;
     goto exit_l;
   }
   
   en = pci_request_regions(pdev,DRV_NAME);
   if(en)
   {
     printk(KERN_ERR DRV_NAME ": pci_request_regions() failed\n");
     ret = -ENODEV;
     goto exit_l;
   }
   
   //csr_addr = pci_iomap(pdev,i,base_start - base_end);
   //csr_addr = pci_iomap(pdev,i,pci_resource_len(pdev,0));
   pdata->registers = pci_iomap(pdev, i, pci_resource_len(pdev, 0));
   if(!pdata->registers)
   {
     printk(KERN_ERR DRV_NAME ": failed to map BAR %d\n",i);
     ret = -ENODEV;
     goto exit_l;
   }
   
   csr_addr = pdata->registers;
   
   led = led_classdev_register(&pdev->dev, &led_dev);
   if(led)
   {
    printk(KERN_ERR DRV_NAME ": Register led CLASS failed: %d\n",led);
    ret = -ENODEV;
    goto exit_l;
   }

exit_l:
   return ret;
}

static void rtbt_pci_remove(struct pci_dev *pdev)
{
  DEFINE_SEMAPHORE(rwsem);
  
  printk(KERN_INFO "rtbt_pci_remove\n");
  down(&rwsem);
  if(hdev)
  {
    hci_unregister_dev(hdev);
    hci_free_dev(hdev);
  }
  led_classdev_unregister(&led_dev);
  if(pdata->registers)
    pci_iounmap(pdev,pdata->registers);
  pci_release_regions(pdev);
  pci_disable_device(pdev);
  if(pdata)
    kfree(pdata);
  up(&rwsem);
}

static int rtbt_pci_suspend(struct pci_dev *pdev, pm_message_t state)
{
    return 0;
}

static int rtbt_pci_resume(struct pci_dev *pdev)
{
    return 0;
}

module_init(example_init);
module_exit(example_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Vladyslav Syroezhkin <vlaomao@gmail.com>");
