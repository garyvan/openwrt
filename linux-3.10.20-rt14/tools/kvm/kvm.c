#include "kvm/kvm.h"
#include "kvm/read-write.h"
#include "kvm/util.h"
#include "kvm/strbuf.h"
#include "kvm/mutex.h"
#include "kvm/kvm-cpu.h"
#include "kvm/kvm-ipc.h"

#include <linux/kernel.h>
#include <linux/kvm.h>
#include <linux/list.h>
#include <linux/err.h>

#include <sys/un.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <stdbool.h>
#include <limits.h>
#include <signal.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <fcntl.h>
#include <time.h>
#include <sys/eventfd.h>
#include <asm/unistd.h>
#include <dirent.h>

#define DEFINE_KVM_EXIT_REASON(reason) [reason] = #reason

const char *kvm_exit_reasons[] = {
	DEFINE_KVM_EXIT_REASON(KVM_EXIT_UNKNOWN),
	DEFINE_KVM_EXIT_REASON(KVM_EXIT_EXCEPTION),
	DEFINE_KVM_EXIT_REASON(KVM_EXIT_IO),
	DEFINE_KVM_EXIT_REASON(KVM_EXIT_HYPERCALL),
	DEFINE_KVM_EXIT_REASON(KVM_EXIT_DEBUG),
	DEFINE_KVM_EXIT_REASON(KVM_EXIT_HLT),
	DEFINE_KVM_EXIT_REASON(KVM_EXIT_MMIO),
	DEFINE_KVM_EXIT_REASON(KVM_EXIT_IRQ_WINDOW_OPEN),
	DEFINE_KVM_EXIT_REASON(KVM_EXIT_SHUTDOWN),
	DEFINE_KVM_EXIT_REASON(KVM_EXIT_FAIL_ENTRY),
	DEFINE_KVM_EXIT_REASON(KVM_EXIT_INTR),
	DEFINE_KVM_EXIT_REASON(KVM_EXIT_SET_TPR),
	DEFINE_KVM_EXIT_REASON(KVM_EXIT_TPR_ACCESS),
	DEFINE_KVM_EXIT_REASON(KVM_EXIT_S390_SIEIC),
	DEFINE_KVM_EXIT_REASON(KVM_EXIT_S390_RESET),
	DEFINE_KVM_EXIT_REASON(KVM_EXIT_DCR),
	DEFINE_KVM_EXIT_REASON(KVM_EXIT_NMI),
	DEFINE_KVM_EXIT_REASON(KVM_EXIT_INTERNAL_ERROR),
#ifdef CONFIG_PPC64
	DEFINE_KVM_EXIT_REASON(KVM_EXIT_PAPR_HCALL),
#endif
};

static int pause_event;
static DEFINE_MUTEX(pause_lock);
extern struct kvm_ext kvm_req_ext[];

static char kvm_dir[PATH_MAX];

static int set_dir(const char *fmt, va_list args)
{
	char tmp[PATH_MAX];

	vsnprintf(tmp, sizeof(tmp), fmt, args);

	mkdir(tmp, 0777);

	if (!realpath(tmp, kvm_dir))
		return -errno;

	strcat(kvm_dir, "/");

	return 0;
}

void kvm__set_dir(const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	set_dir(fmt, args);
	va_end(args);
}

const char *kvm__get_dir(void)
{
	return kvm_dir;
}

bool kvm__supports_extension(struct kvm *kvm, unsigned int extension)
{
	int ret;

	ret = ioctl(kvm->sys_fd, KVM_CHECK_EXTENSION, extension);
	if (ret < 0)
		return false;

	return ret;
}

static int kvm__check_extensions(struct kvm *kvm)
{
	int i;

	for (i = 0; ; i++) {
		if (!kvm_req_ext[i].name)
			break;
		if (!kvm__supports_extension(kvm, kvm_req_ext[i].code)) {
			pr_err("Unsuppored KVM extension detected: %s",
				kvm_req_ext[i].name);
			return -i;
		}
	}

	return 0;
}

struct kvm *kvm__new(void)
{
	struct kvm *kvm = calloc(1, sizeof(*kvm));
	if (!kvm)
		return ERR_PTR(-ENOMEM);

	kvm->sys_fd = -1;
	kvm->vm_fd = -1;

	return kvm;
}

int kvm__exit(struct kvm *kvm)
{
	struct kvm_mem_bank *bank, *tmp;

	kvm__arch_delete_ram(kvm);

	list_for_each_entry_safe(bank, tmp, &kvm->mem_banks, list) {
		list_del(&bank->list);
		free(bank);
	}

	free(kvm);
	return 0;
}
core_exit(kvm__exit);

/*
 * Note: KVM_SET_USER_MEMORY_REGION assumes that we don't pass overlapping
 * memory regions to it. Therefore, be careful if you use this function for
 * registering memory regions for emulating hardware.
 */
int kvm__register_mem(struct kvm *kvm, u64 guest_phys, u64 size, void *userspace_addr)
{
	struct kvm_userspace_memory_region mem;
	struct kvm_mem_bank *bank;
	int ret;

	bank = malloc(sizeof(*bank));
	if (!bank)
		return -ENOMEM;

	INIT_LIST_HEAD(&bank->list);
	bank->guest_phys_addr		= guest_phys;
	bank->host_addr			= userspace_addr;
	bank->size			= size;

	mem = (struct kvm_userspace_memory_region) {
		.slot			= kvm->mem_slots++,
		.guest_phys_addr	= guest_phys,
		.memory_size		= size,
		.userspace_addr		= (unsigned long)userspace_addr,
	};

	ret = ioctl(kvm->vm_fd, KVM_SET_USER_MEMORY_REGION, &mem);
	if (ret < 0)
		return -errno;

	list_add(&bank->list, &kvm->mem_banks);
	return 0;
}

void *guest_flat_to_host(struct kvm *kvm, u64 offset)
{
	struct kvm_mem_bank *bank;

	list_for_each_entry(bank, &kvm->mem_banks, list) {
		u64 bank_start = bank->guest_phys_addr;
		u64 bank_end = bank_start + bank->size;

		if (offset >= bank_start && offset < bank_end)
			return bank->host_addr + (offset - bank_start);
	}

	pr_warning("unable to translate guest address 0x%llx to host",
			(unsigned long long)offset);
	return NULL;
}

u64 host_to_guest_flat(struct kvm *kvm, void *ptr)
{
	struct kvm_mem_bank *bank;

	list_for_each_entry(bank, &kvm->mem_banks, list) {
		void *bank_start = bank->host_addr;
		void *bank_end = bank_start + bank->size;

		if (ptr >= bank_start && ptr < bank_end)
			return bank->guest_phys_addr + (ptr - bank_start);
	}

	pr_warning("unable to translate host address %p to guest", ptr);
	return 0;
}

int kvm__recommended_cpus(struct kvm *kvm)
{
	int ret;

	ret = ioctl(kvm->sys_fd, KVM_CHECK_EXTENSION, KVM_CAP_NR_VCPUS);
	if (ret <= 0)
		/*
		 * api.txt states that if KVM_CAP_NR_VCPUS does not exist,
		 * assume 4.
		 */
		return 4;

	return ret;
}

/*
 * The following hack should be removed once 'x86: Raise the hard
 * VCPU count limit' makes it's way into the mainline.
 */
#ifndef KVM_CAP_MAX_VCPUS
#define KVM_CAP_MAX_VCPUS 66
#endif

int kvm__max_cpus(struct kvm *kvm)
{
	int ret;

	ret = ioctl(kvm->sys_fd, KVM_CHECK_EXTENSION, KVM_CAP_MAX_VCPUS);
	if (ret <= 0)
		ret = kvm__recommended_cpus(kvm);

	return ret;
}

int kvm__init(struct kvm *kvm)
{
	int ret;

	if (!kvm__arch_cpu_supports_vm()) {
		pr_err("Your CPU does not support hardware virtualization");
		ret = -ENOSYS;
		goto err;
	}

	kvm->sys_fd = open(kvm->cfg.dev, O_RDWR);
	if (kvm->sys_fd < 0) {
		if (errno == ENOENT)
			pr_err("'%s' not found. Please make sure your kernel has CONFIG_KVM "
			       "enabled and that the KVM modules are loaded.", kvm->cfg.dev);
		else if (errno == ENODEV)
			pr_err("'%s' KVM driver not available.\n  # (If the KVM "
			       "module is loaded then 'dmesg' may offer further clues "
			       "about the failure.)", kvm->cfg.dev);
		else
			pr_err("Could not open %s: ", kvm->cfg.dev);

		ret = -errno;
		goto err_free;
	}

	ret = ioctl(kvm->sys_fd, KVM_GET_API_VERSION, 0);
	if (ret != KVM_API_VERSION) {
		pr_err("KVM_API_VERSION ioctl");
		ret = -errno;
		goto err_sys_fd;
	}

	kvm->vm_fd = ioctl(kvm->sys_fd, KVM_CREATE_VM, 1);
	if (kvm->vm_fd < 0) {
		pr_err("KVM_CREATE_VM ioctl");
		ret = kvm->vm_fd;
		goto err_sys_fd;
	}

	if (kvm__check_extensions(kvm)) {
		pr_err("A required KVM extension is not supported by OS");
		ret = -ENOSYS;
		goto err_vm_fd;
	}

	kvm__arch_init(kvm, kvm->cfg.hugetlbfs_path, kvm->cfg.ram_size);

	INIT_LIST_HEAD(&kvm->mem_banks);
	kvm__init_ram(kvm);

	if (!kvm->cfg.firmware_filename) {
		if (!kvm__load_kernel(kvm, kvm->cfg.kernel_filename,
				kvm->cfg.initrd_filename, kvm->cfg.real_cmdline))
			die("unable to load kernel %s", kvm->cfg.kernel_filename);
	}

	if (kvm->cfg.firmware_filename) {
		if (!kvm__load_firmware(kvm, kvm->cfg.firmware_filename))
			die("unable to load firmware image %s: %s", kvm->cfg.firmware_filename, strerror(errno));
	} else {
		ret = kvm__arch_setup_firmware(kvm);
		if (ret < 0)
			die("kvm__arch_setup_firmware() failed with error %d\n", ret);
	}

	return 0;

err_vm_fd:
	close(kvm->vm_fd);
err_sys_fd:
	close(kvm->sys_fd);
err_free:
	free(kvm);
err:
	return ret;
}
core_init(kvm__init);

/* RFC 1952 */
#define GZIP_ID1		0x1f
#define GZIP_ID2		0x8b
#define CPIO_MAGIC		"0707"
/* initrd may be gzipped, or a plain cpio */
static bool initrd_check(int fd)
{
	unsigned char id[4];

	if (read_in_full(fd, id, ARRAY_SIZE(id)) < 0)
		return false;

	if (lseek(fd, 0, SEEK_SET) < 0)
		die_perror("lseek");

	return (id[0] == GZIP_ID1 && id[1] == GZIP_ID2) ||
		!memcmp(id, CPIO_MAGIC, 4);
}

int load_elf_binary(struct kvm *kvm, int fd_kernel, int fd_initrd, const char *kernel_cmdline) __attribute__((__weak__));
int load_elf_binary(struct kvm *kvm, int fd_kernel, int fd_initrd, const char *kernel_cmdline)
{
	return false;
}

bool kvm__load_kernel(struct kvm *kvm, const char *kernel_filename,
		const char *initrd_filename, const char *kernel_cmdline)
{
	bool ret;
	int fd_kernel = -1, fd_initrd = -1;

	fd_kernel = open(kernel_filename, O_RDONLY);
	if (fd_kernel < 0)
		die("Unable to open kernel %s", kernel_filename);

	if (initrd_filename) {
		fd_initrd = open(initrd_filename, O_RDONLY);
		if (fd_initrd < 0)
			die("Unable to open initrd %s", initrd_filename);

		if (!initrd_check(fd_initrd))
			die("%s is not an initrd", initrd_filename);
	}

	ret = load_bzimage(kvm, fd_kernel, fd_initrd, kernel_cmdline);

	if (ret)
		goto found_kernel;

	pr_warning("%s is not a bzImage. Trying to load it as a flat binary...", kernel_filename);

	ret = load_elf_binary(kvm, fd_kernel, fd_initrd, kernel_cmdline);

	if (ret)
		goto found_kernel;

	ret = load_flat_binary(kvm, fd_kernel, fd_initrd, kernel_cmdline);

	if (ret)
		goto found_kernel;


	if (initrd_filename)
		close(fd_initrd);
	close(fd_kernel);

	die("%s is not a valid bzImage or flat binary", kernel_filename);

found_kernel:
	if (initrd_filename)
		close(fd_initrd);
	close(fd_kernel);

	return ret;
}

#define TIMER_INTERVAL_NS 1000000	/* 1 msec */

/*
 * This function sets up a timer that's used to inject interrupts from the
 * userspace hypervisor into the guest at periodical intervals. Please note
 * that clock interrupt, for example, is not handled here.
 */
int kvm_timer__init(struct kvm *kvm)
{
	struct itimerspec its;
	struct sigevent sev;
	int r;

	memset(&sev, 0, sizeof(struct sigevent));
	sev.sigev_value.sival_int	= 0;
	sev.sigev_notify		= SIGEV_THREAD_ID;
	sev.sigev_signo			= SIGALRM;
	sev.sigev_value.sival_ptr	= kvm;
	sev._sigev_un._tid		= syscall(__NR_gettid);

	r = timer_create(CLOCK_REALTIME, &sev, &kvm->timerid);
	if (r < 0)
		return r;

	its.it_value.tv_sec		= TIMER_INTERVAL_NS / 1000000000;
	its.it_value.tv_nsec		= TIMER_INTERVAL_NS % 1000000000;
	its.it_interval.tv_sec		= its.it_value.tv_sec;
	its.it_interval.tv_nsec		= its.it_value.tv_nsec;

	r = timer_settime(kvm->timerid, 0, &its, NULL);
	if (r < 0) {
		timer_delete(kvm->timerid);
		return r;
	}

	return 0;
}
firmware_init(kvm_timer__init);

int kvm_timer__exit(struct kvm *kvm)
{
	if (kvm->timerid)
		if (timer_delete(kvm->timerid) < 0)
			die("timer_delete()");

	kvm->timerid = 0;

	return 0;
}
firmware_exit(kvm_timer__exit);

void kvm__dump_mem(struct kvm *kvm, unsigned long addr, unsigned long size, int debug_fd)
{
	unsigned char *p;
	unsigned long n;

	size &= ~7; /* mod 8 */
	if (!size)
		return;

	p = guest_flat_to_host(kvm, addr);

	for (n = 0; n < size; n += 8) {
		if (!host_ptr_in_ram(kvm, p + n)) {
			dprintf(debug_fd, " 0x%08lx: <unknown>\n", addr + n);
			continue;
		}
		dprintf(debug_fd, " 0x%08lx: %02x %02x %02x %02x  %02x %02x %02x %02x\n",
			addr + n, p[n + 0], p[n + 1], p[n + 2], p[n + 3],
				  p[n + 4], p[n + 5], p[n + 6], p[n + 7]);
	}
}

void kvm__pause(struct kvm *kvm)
{
	int i, paused_vcpus = 0;

	/* Check if the guest is running */
	if (!kvm->cpus[0] || kvm->cpus[0]->thread == 0)
		return;

	mutex_lock(&pause_lock);

	pause_event = eventfd(0, 0);
	if (pause_event < 0)
		die("Failed creating pause notification event");
	for (i = 0; i < kvm->nrcpus; i++)
		pthread_kill(kvm->cpus[i]->thread, SIGKVMPAUSE);

	while (paused_vcpus < kvm->nrcpus) {
		u64 cur_read;

		if (read(pause_event, &cur_read, sizeof(cur_read)) < 0)
			die("Failed reading pause event");
		paused_vcpus += cur_read;
	}
	close(pause_event);
}

void kvm__continue(struct kvm *kvm)
{
	/* Check if the guest is running */
	if (!kvm->cpus[0] || kvm->cpus[0]->thread == 0)
		return;

	mutex_unlock(&pause_lock);
}

void kvm__notify_paused(void)
{
	u64 p = 1;

	if (write(pause_event, &p, sizeof(p)) < 0)
		die("Failed notifying of paused VCPU.");

	mutex_lock(&pause_lock);
	mutex_unlock(&pause_lock);
}
