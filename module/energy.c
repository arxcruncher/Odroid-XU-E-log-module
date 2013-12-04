/*
 * Linux Kernel Logging Module
 *
 * Copyright (C) 2013. Karel De Vogleer, ParisTech, Institute Mines-Telecom.
 *
 * Contact information for redistribution (binary or source code):
 * Karel De Vogeleer (karel@devogeleer.be)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 * 
 */

#undef __KERNEL__
#define __KERNEL__
 
#undef MODULE
#define MODULE
 
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/uaccess.h>
#include <linux/workqueue.h>
#include <linux/spinlock.h>
#include <linux/nmi.h>
#include <linux/io.h>
#include <linux/list.h>
#include <linux/types.h>

#include <linux/cpufreq.h>
#include <linux/thermal.h>
#include <linux/platform_data/exynos_thermal.h>
#include <linux/platform_data/ina231.h>
#include <linux/clk.h>

#define MAX_ENTRIES 32
#define MAX_ENTRY_LENGTH 128
#define MAX_TEMP_BUFFER_LENGTH 128
#define SAMPLING_FREQ 200

unsigned int index_log;
int index_last_read;
int index_total;
unsigned long my_sampling_frequency;
spinlock_t my_lock;

char my_log[MAX_ENTRIES][MAX_ENTRY_LENGTH]; 
char my_buffer[MAX_TEMP_BUFFER_LENGTH];

static struct workqueue_struct *my_wq = 0;
static void mykmod_work_handler(struct work_struct *w);
static DECLARE_DELAYED_WORK(mykmod_work, mykmod_work_handler);

struct proc_dir_entry *my_proc_dir = NULL;
struct proc_dir_entry *my_proc_read = NULL;
struct proc_dir_entry *my_proc_config = NULL;
struct proc_dir_entry *my_proc_status = NULL;

// Predefined Structures --------------------------

#define MAX_COOLING_DEVICE 4
#define MAX_TRIP_COUNT 8
#define SENSOR_NAME_LEN 16
#define EXYNOS_TMU_REG_CURRENT_TEMP	0x40
#define EXYNOS_TMU_COUNT 4
#define EXYNOS_TMU_DEF_CODE_TO_TEMP_OFFSET 50

//ParisTech
struct measurement_reading {
	unsigned int a7_cur_uA;
	unsigned int a7_cur_uV;
	unsigned int a7_cur_uW;
	unsigned int a15_cur_uA;
	unsigned int a15_cur_uV;
	unsigned int a15_cur_uW;
};

extern struct exynos_tmu_data* exynos_thermal_get_tmu_data(void);
extern void ina231_get_sensor_values(struct measurement_reading *my_measurement_reading);

LIST_HEAD(sensor_list);
static struct exynos_tmu_data* data;

static void exynos_thermal_get_value(struct my_temp_data *my_data)
{
	u8 temp_code;
	int i = 0;

	for (i = 0; i < EXYNOS_TMU_COUNT; i++) {
		temp_code = readb(data->base[i] + EXYNOS_TMU_REG_CURRENT_TEMP);
		if (temp_code == 0xff)
			continue;
		my_data->temp[i]   = temp_code;
		my_data->error1[i] = data->temp_error1[i];
		my_data->error2[i] = data->temp_error2[i];
	}
}

static ssize_t write_temperature(char *buf)
{	
	struct my_temp_data t_temp;
	int i, min, max;
	ssize_t add_length = 0;
	
	min = 46;
	max = 146;
	exynos_thermal_get_value(&t_temp);
	
	for(i = 0; i < EXYNOS_TMU_COUNT; i++) {
		int temp   = (int)t_temp.temp[i];
		int error1 = (int)t_temp.error1[i];
		int error2 = (int)t_temp.error2[i];
		
		/* temp_code should range between min and max */
		if (temp < min || temp > max) {
			temp = -ENODATA;
			goto out;
		}
		
		add_length += sprintf(&buf[add_length], " %u %u %u", temp, error1, error2);
	}

out:
	return add_length;
}

static ssize_t write_power(char *buf)
{	
	ssize_t add_length = 0;
	struct measurement_reading my_measurement_reading;
	
	ina231_get_sensor_values(&my_measurement_reading);
	
	add_length += sprintf(&buf[add_length], " %u", \
		my_measurement_reading.a7_cur_uA);
	add_length += sprintf(&buf[add_length], " %u", \
		my_measurement_reading.a7_cur_uV);
	add_length += sprintf(&buf[add_length], " %u", \
		my_measurement_reading.a7_cur_uW);
	add_length += sprintf(&buf[add_length], " %u", \
		my_measurement_reading.a15_cur_uA);
	add_length += sprintf(&buf[add_length], " %u", \
		my_measurement_reading.a15_cur_uV);
	add_length += sprintf(&buf[add_length], " %u", \
		my_measurement_reading.a15_cur_uW);
	
	return add_length;
}

static void mykmod_work_handler(struct work_struct *w)
{
	int this_cpu, tlen, freq, i;
	unsigned long long t;
	unsigned long nanosec_rem;
	
	this_cpu = smp_processor_id();
	
	// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	
	spin_lock(&my_lock);

	// Write time
	t = cpu_clock(this_cpu);
	nanosec_rem = do_div(t, 1000000000);
	tlen = sprintf(my_log[index_log], "[%5lu.%06lu] %u",
					(unsigned long) t,
					nanosec_rem / 1000,
					index_total);

	// Write CPU freq	
	for(i = 0; i < NR_CPUS; i++) {
		freq = (int)(cpufreq_quick_get(i)/1000);
		tlen += sprintf(my_log[index_log] + tlen, " %i", freq);
	}

	// Write Temperature
	tlen += write_temperature(my_log[index_log] + tlen);
	
	// Write Power
	tlen += write_power(my_log[index_log] + tlen);
	
	// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	
	tlen += sprintf(my_log[index_log] + tlen, "\n");

	index_total++;
	index_log++;
	if(index_log >= MAX_ENTRIES)
			index_log = 0;
	
	spin_unlock(&my_lock);
	
	// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	
	if (my_wq)
		queue_delayed_work(my_wq, &mykmod_work, my_sampling_frequency);
}

static int log_show_data(struct seq_file *m, void *v)
{	
	return 0;
}

static int log_show_config(struct seq_file *m, void *v)
{
	return 0;
}
	
static int log_show_status(struct seq_file *m, void *v)
{
	spin_lock(&my_lock);
	seq_printf(m, "index_total: %u, index_log: %i, index_last_read: %i.\n", index_total, index_log, index_last_read);
	spin_unlock(&my_lock);
	
	return 0;
}

static int log_read_open(struct inode *inode, struct  file *file)
{
	return single_open(file, log_show_data, NULL);
}

static int log_config_open(struct inode *inode, struct  file *file)
{
	return single_open(file, log_show_config, NULL);
}

static ssize_t log_config_write (struct file *file, const char *buffer, size_t len, loff_t * off)
{
	static unsigned long procfs_buffer_size = 0;
	
	if (len > MAX_TEMP_BUFFER_LENGTH) {
		procfs_buffer_size = MAX_TEMP_BUFFER_LENGTH;
	}
	else {
		procfs_buffer_size = len;
	}
	
	if (copy_from_user(my_buffer, buffer, procfs_buffer_size)) {
		return -EFAULT;
	}

	printk(KERN_INFO "ParisTech: write %lu bytes\n", procfs_buffer_size);
	
	return procfs_buffer_size;
}

ssize_t my_read (struct file *fp, char __user *buf, size_t count, loff_t *offset)  
{
	ssize_t add_length = 0;
	char tmpbuf[MAX_ENTRIES*MAX_ENTRY_LENGTH];
	
	// Print the lines that were not printed before
	spin_lock(&my_lock);
	while(index_last_read != index_log) {
		add_length += sprintf(&tmpbuf[add_length], "%s", my_log[index_last_read]);
		
		index_last_read++;
		if(index_last_read >= MAX_ENTRIES)
			index_last_read = 0;
	}
	spin_unlock(&my_lock);
	
	return simple_read_from_buffer(buf, count, offset, tmpbuf, add_length);
}  


static int log_status_open(struct inode *inode, struct  file *file)
{
	return single_open(file, log_show_status, NULL);
}

static const struct file_operations read_proc_fops = {
	.owner = THIS_MODULE,
	.open = log_read_open,
	.read = my_read,
	.release = single_release,
};

static const struct file_operations status_proc_fops = {
	.owner = THIS_MODULE,
	.open = log_status_open,
	.read = seq_read,
	.release = single_release,
};

static const struct file_operations config_proc_fops = {
	.owner = THIS_MODULE,
	.open = log_config_open,
	.read = seq_read,
	.release = single_release,
};

// Initialize the /proc file hierarchy
static void make_proc_dir(void)
{    
	/* Create a new directory */
	my_proc_dir = create_proc_entry("paristech", S_IFDIR, NULL );

	if (my_proc_dir) {
		/* No Read and Write functions here */
		my_proc_dir->read_proc  = NULL;
		my_proc_dir->write_proc = NULL;
	}
   
	my_proc_read   = proc_create("read",   0644, my_proc_dir, &read_proc_fops);
	my_proc_config = proc_create("config", 0644, my_proc_dir, &config_proc_fops);
	my_proc_status = proc_create("status", 0644, my_proc_dir, &status_proc_fops);   
}

static int __init log_init(void)
{   
	index_last_read = 0;
	index_log = 0;
	index_total = 0;
	
	// Initialize /proc filesystem
	make_proc_dir();
	
	spin_lock_init(&my_lock);
	
	data = exynos_thermal_get_tmu_data();
	
	// Initialize workqueue
	my_sampling_frequency = msecs_to_jiffies(SAMPLING_FREQ);
	if (!my_wq)
		my_wq = create_singlethread_workqueue("paristech-log");
	if (my_wq)
		queue_delayed_work(my_wq, &mykmod_work, my_sampling_frequency);
	
    printk(KERN_INFO "ParisTech: logging module loaded!\n");
    return 0;    // Non-zero return means that the module couldn't be loaded.
}
 
static void __exit log_cleanup(void)
{	
	// Close workqueue
	if (my_wq) {
		cancel_delayed_work_sync(&mykmod_work);
		destroy_workqueue(my_wq);
	}
	
	// Delete the /proc file hierarchy
	remove_proc_entry("paristech/read", NULL);
	remove_proc_entry("paristech/config", NULL);
	remove_proc_entry("paristech/status", NULL);
	remove_proc_entry("paristech", NULL);
	
    printk(KERN_INFO "ParisTech: cleaning up logging module.\n");
}
 
module_init(log_init);
module_exit(log_cleanup);

MODULE_DESCRIPTION("ParisTech performance logging module.");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Karel De Vogeleer");
