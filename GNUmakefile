# Nuke built-in rules.
.SUFFIXES:

# Target architecture to build for. Default to x86_64.
ARCH := x86_64

# Default user QEMU flags. These are appended to the QEMU command calls.
QEMUFLAGS := -m 2G -d int -device isa-debugcon,chardev=debug -chardev stdio,id=debug

override IMAGE_NAME := template-$(ARCH)

# Toolchain for building the 'limine' executable for the host.
HOST_CC := cc
HOST_CFLAGS := -g -O2 -pipe -c23
HOST_CPPFLAGS := 
HOST_LDFLAGS :=
HOST_LIBS :=

# Kernel build dependencies (formerly kernel/get-deps).
FREESTANDING_HDRS_REPO   := https://github.com/osdev0/freestanding-c-hdrs.git
FREESTANDING_HDRS_COMMIT := 38fed4e1e3365733ddbfa03b0a28936243ad31e9

CC_RUNTIME_REPO   := https://github.com/osdev0/cc-runtime.git
CC_RUNTIME_COMMIT := dae79833b57a01b9fd3e359ee31def69f5ae899b

LIMINE_PROTOCOL_REPO   := https://github.com/Limine-Bootloader/limine-protocol.git
LIMINE_PROTOCOL_COMMIT := 80ef54bed402b8c0b672a707c1df4c532f3428ad

.PHONY: all
all: $(IMAGE_NAME).iso

.PHONY: all-hdd
all-hdd: $(IMAGE_NAME).hdd

.PHONY: run
run: run-$(ARCH)

.PHONY: run-hdd
run-hdd: run-hdd-$(ARCH)

.PHONY: run-x86_64
run-x86_64: edk2-ovmf-bins $(IMAGE_NAME).iso disk.img
	qemu-system-$(ARCH) \
		-M q35 \
		-drive if=pflash,unit=0,format=raw,file=edk2-ovmf-bins/ovmf-code-$(ARCH).fd,readonly=on \
		-cdrom $(IMAGE_NAME).iso \
		-drive id=disk0,if=none,format=raw,file=disk.img \
		-device ahci,id=ahci0 \
		-device ide-hd,drive=disk0,bus=ahci0.0 \
		$(QEMUFLAGS)

disk.img:
	dd if=/dev/zero of=disk.img bs=1M count=64

.PHONY: run-hdd-x86_64
run-hdd-x86_64: edk2-ovmf-bins $(IMAGE_NAME).hdd
	qemu-system-$(ARCH) \
		-M q35 \
		-drive if=pflash,unit=0,format=raw,file=edk2-ovmf-bins/ovmf-code-$(ARCH).fd,readonly=on \
		-hda $(IMAGE_NAME).hdd \
		$(QEMUFLAGS)

.PHONY: run-aarch64
run-aarch64: edk2-ovmf-bins $(IMAGE_NAME).iso
	qemu-system-$(ARCH) \
		-M virt \
		-cpu cortex-a72 \
		-device ramfb \
		-device qemu-xhci \
		-device usb-kbd \
		-device usb-tablet \
		-drive if=pflash,unit=0,format=raw,file=edk2-ovmf-bins/ovmf-code-$(ARCH).fd,readonly=on \
		-cdrom $(IMAGE_NAME).iso \
		$(QEMUFLAGS)

.PHONY: run-hdd-aarch64
run-hdd-aarch64: edk2-ovmf-bins $(IMAGE_NAME).hdd
	qemu-system-$(ARCH) \
		-M virt \
		-cpu cortex-a72 \
		-device ramfb \
		-device qemu-xhci \
		-device usb-kbd \
		-device usb-tablet \
		-drive if=pflash,unit=0,format=raw,file=edk2-ovmf-bins/ovmf-code-$(ARCH).fd,readonly=on \
		-hda $(IMAGE_NAME).hdd \
		$(QEMUFLAGS)

.PHONY: run-riscv64
run-riscv64: edk2-ovmf-bins $(IMAGE_NAME).iso
	qemu-system-$(ARCH) \
		-M virt \
		-cpu rv64 \
		-device ramfb \
		-device qemu-xhci \
		-device usb-kbd \
		-device usb-tablet \
		-drive if=pflash,unit=0,format=raw,file=edk2-ovmf-bins/ovmf-code-$(ARCH).fd,readonly=on \
		-cdrom $(IMAGE_NAME).iso \
		$(QEMUFLAGS)

.PHONY: run-hdd-riscv64
run-hdd-riscv64: edk2-ovmf-bins $(IMAGE_NAME).hdd
	qemu-system-$(ARCH) \
		-M virt \
		-cpu rv64 \
		-device ramfb \
		-device qemu-xhci \
		-device usb-kbd \
		-device usb-tablet \
		-drive if=pflash,unit=0,format=raw,file=edk2-ovmf-bins/ovmf-code-$(ARCH).fd,readonly=on \
		-hda $(IMAGE_NAME).hdd \
		$(QEMUFLAGS)

.PHONY: run-loongarch64
run-loongarch64: edk2-ovmf-bins $(IMAGE_NAME).iso
	qemu-system-$(ARCH) \
		-M virt \
		-cpu la464 \
		-device ramfb \
		-device qemu-xhci \
		-device usb-kbd \
		-device usb-tablet \
		-drive if=pflash,unit=0,format=raw,file=edk2-ovmf-bins/ovmf-code-$(ARCH).fd,readonly=on \
		-cdrom $(IMAGE_NAME).iso \
		$(QEMUFLAGS)

.PHONY: run-hdd-loongarch64
run-hdd-loongarch64: edk2-ovmf-bins $(IMAGE_NAME).hdd
	qemu-system-$(ARCH) \
		-M virt \
		-cpu la464 \
		-device ramfb \
		-device qemu-xhci \
		-device usb-kbd \
		-device usb-tablet \
		-drive if=pflash,unit=0,format=raw,file=edk2-ovmf-bins/ovmf-code-$(ARCH).fd,readonly=on \
		-hda $(IMAGE_NAME).hdd \
		$(QEMUFLAGS)


.PHONY: run-bios
run-bios: $(IMAGE_NAME).iso
	qemu-system-$(ARCH) \
		-M q35 \
		-cdrom $(IMAGE_NAME).iso \
		-boot d \
		$(QEMUFLAGS)

.PHONY: run-hdd-bios
run-hdd-bios: $(IMAGE_NAME).hdd
	qemu-system-$(ARCH) \
		-M q35 \
		-hda $(IMAGE_NAME).hdd \
		$(QEMUFLAGS)

.PHONY: mlibc
mlibc:
	rm -rf mlibc/build
	
	cd mlibc && meson setup build \
		--cross-file crossfile \
		-Ddefault_library=static \
		-Db_staticpic=false \
		-Dposix_option=enabled \
		--prefix=/usr/local
	
	cd mlibc && ninja -C build
	
	cd mlibc && DESTDIR=$$(pwd)/build/install ninja -C build install

.PHONY: userspace
userspace: mlibc
	$(MAKE) -C userspace clean
	$(MAKE) -C userspace

edk2-ovmf-bins:
	curl -L https://github.com/osdev0/edk2-ovmf-stable-bins/releases/latest/download/edk2-ovmf-bins.tar.gz | gunzip | tar -xf -

bootloader/limine-binary/limine:
	rm -rf bootloader/limine-binary
	curl -L https://github.com/Limine-Bootloader/Limine/releases/latest/download/limine-binary.tar.gz | gunzip | tar -xf -
	$(MAKE) -C bootloader/limine-binary \
		CC="$(HOST_CC)" \
		CFLAGS="$(HOST_CFLAGS)" \
		CPPFLAGS="$(HOST_CPPFLAGS)" \
		LDFLAGS="$(HOST_LDFLAGS)" \
		LIBS="$(HOST_LIBS)"
kernel/.deps-obtained:
	@set -e; \
	clone_repo_commit() { \
		repo_url="$$1"; dest="$$2"; commit="$$3"; \
		if test -d "$$dest/.git"; then \
			git -C "$$dest" reset --hard; \
			git -C "$$dest" clean -fd; \
			if ! git -C "$$dest" -c advice.detachedHead=false checkout "$$commit"; then \
				rm -rf "$$dest"; \
			fi; \
		elif test -d "$$dest"; then \
			echo "error: '$$dest' is not a Git repository" 1>&2; \
			exit 1; \
		fi; \
		if ! test -d "$$dest"; then \
			git clone "$$repo_url" "$$dest"; \
			if ! git -C "$$dest" -c advice.detachedHead=false checkout "$$commit"; then \
				rm -rf "$$dest"; \
				exit 1; \
			fi; \
		fi; \
	}; \
	rm -f kernel/.deps-obtained; \
	clone_repo_commit "$(FREESTANDING_HDRS_REPO)" kernel/freestanding-c-hdrs "$(FREESTANDING_HDRS_COMMIT)"; \
	clone_repo_commit "$(CC_RUNTIME_REPO)" kernel/cc-runtime "$(CC_RUNTIME_COMMIT)"; \
	clone_repo_commit "$(LIMINE_PROTOCOL_REPO)" kernel/limine-protocol "$(LIMINE_PROTOCOL_COMMIT)"; \
	touch kernel/.deps-obtained
	@printf "\nDependencies obtained successfully.\n"

.PHONY: clone
clone:
	@set -e; \
	clone_repo_commit() { \
		repo_url="$$1"; dest="$$2"; commit="$$3"; \
		if test -d "$$dest/.git"; then \
			git -C "$$dest" reset --hard; \
			git -C "$$dest" clean -fd; \
			if ! git -C "$$dest" -c advice.detachedHead=false checkout "$$commit"; then \
				rm -rf "$$dest"; \
			fi; \
		elif test -d "$$dest"; then \
			echo "error: '$$dest' is not a Git repository" 1>&2; \
			exit 1; \
		fi; \
		if ! test -d "$$dest"; then \
			git clone "$$repo_url" "$$dest"; \
			if ! git -C "$$dest" -c advice.detachedHead=false checkout "$$commit"; then \
				rm -rf "$$dest"; \
				exit 1; \
			fi; \
		fi; \
	}; \
	clone_repo_commit "$(FREESTANDING_HDRS_REPO)" kernel/freestanding-c-hdrs "$(FREESTANDING_HDRS_COMMIT)"; \
	clone_repo_commit "$(CC_RUNTIME_REPO)" kernel/cc-runtime "$(CC_RUNTIME_COMMIT)"; \
	clone_repo_commit "$(LIMINE_PROTOCOL_REPO)" kernel/limine-protocol "$(LIMINE_PROTOCOL_COMMIT)"; \
	touch kernel/.deps-obtained
	@printf "\nDependencies obtained successfully.\n"

.PHONY: kernel
kernel: kernel/.deps-obtained
	$(MAKE) -C kernel

userspace.tar: userspace/GNUmakefile $(wildcard userspace/*.c userspace/*.ld userspace/usr/src/*.c) tools/tcc-0.9.27/tcc tools/tcc-0.9.27/libtcc1.a
	$(MAKE) -C userspace
	tar --format=ustar -C userspace -cf $@ .

$(IMAGE_NAME).iso: bootloader/limine-binary/limine kernel userspace.tar
	rm -rf iso_root
	mkdir -p iso_root/boot
	cp -v kernel/bin-$(ARCH)/kernel iso_root/boot/
	cp -v userspace.tar iso_root/
	mkdir -p iso_root/boot/limine
	cp -v bootloader/limine.conf iso_root/boot/limine/
	mkdir -p iso_root/EFI/BOOT
ifeq ($(ARCH),x86_64)
	cp -v bootloader/limine-binary/limine-bios.sys bootloader/limine-binary/limine-bios-cd.bin bootloader/limine-binary/limine-uefi-cd.bin iso_root/boot/limine/
	cp -v bootloader/limine-binary/BOOTX64.EFI iso_root/EFI/BOOT/
	cp -v bootloader/limine-binary/BOOTIA32.EFI iso_root/EFI/BOOT/
	xorriso -as mkisofs -R -r -b boot/limine/limine-bios-cd.bin \
		-no-emul-boot -boot-load-size 4 -boot-info-table -hfsplus \
		-apm-block-size 2048 --efi-boot boot/limine/limine-uefi-cd.bin \
		-efi-boot-part --efi-boot-image --protective-msdos-label \
		iso_root -o $(IMAGE_NAME).iso
	./bootloader/limine-binary/limine bios-install $(IMAGE_NAME).iso
endif
ifeq ($(ARCH),aarch64)
	cp -v bootloader/limine-binary/limine-uefi-cd.bin iso_root/boot/limine/
	cp -v bootloader/limine-binary/BOOTAA64.EFI iso_root/EFI/BOOT/
	xorriso -as mkisofs -R -r -J \
		-hfsplus -apm-block-size 2048 \
		--efi-boot boot/limine/limine-uefi-cd.bin \
		-efi-boot-part --efi-boot-image --protective-msdos-label \
		iso_root -o $(IMAGE_NAME).iso
endif
ifeq ($(ARCH),riscv64)
	cp -v bootloader/limine-binary/limine-uefi-cd.bin iso_root/boot/limine/
	cp -v bootloader/limine-binary/BOOTRISCV64.EFI iso_root/EFI/BOOT/
	xorriso -as mkisofs -R -r -J \
		-hfsplus -apm-block-size 2048 \
		--efi-boot boot/limine/limine-uefi-cd.bin \
		-efi-boot-part --efi-boot-image --protective-msdos-label \
		iso_root -o $(IMAGE_NAME).iso
endif
ifeq ($(ARCH),loongarch64)
	cp -v bootloader/limine-binary/limine-uefi-cd.bin iso_root/boot/limine/
	cp -v bootloader/limine-binary/BOOTLOONGARCH64.EFI iso_root/EFI/BOOT/
	xorriso -as mkisofs -R -r -J \
		-hfsplus -apm-block-size 2048 \
		--efi-boot boot/limine/limine-uefi-cd.bin \
		-efi-boot-part --efi-boot-image --protective-msdos-label \
		iso_root -o $(IMAGE_NAME).iso
endif
	rm -rf iso_root

$(IMAGE_NAME).hdd: bootloader/limine-binary/limine kernel
	rm -f $(IMAGE_NAME).hdd
	dd if=/dev/zero bs=1M count=0 seek=64 of=$(IMAGE_NAME).hdd
ifeq ($(ARCH),x86_64)
	PATH=$$PATH:/usr/sbin:/sbin sgdisk $(IMAGE_NAME).hdd -n 1:2048 -t 1:ef00 -m 1
	./bootloader/limine-binary/limine bios-install $(IMAGE_NAME).hdd
else
	PATH=$$PATH:/usr/sbin:/sbin sgdisk $(IMAGE_NAME).hdd -n 1:2048 -t 1:ef00
endif
	mformat -i $(IMAGE_NAME).hdd@@1M
	mmd -i $(IMAGE_NAME).hdd@@1M ::/EFI ::/EFI/BOOT ::/boot ::/boot/limine
	mcopy -i $(IMAGE_NAME).hdd@@1M kernel/bin-$(ARCH)/kernel ::/boot
	mcopy -i $(IMAGE_NAME).hdd@@1M bootloader/limine.conf ::/boot/limine
ifeq ($(ARCH),x86_64)
	mcopy -i $(IMAGE_NAME).hdd@@1M bootloader/limine-binary/limine-bios.sys ::/boot/limine
	mcopy -i $(IMAGE_NAME).hdd@@1M bootloader/limine-binary/BOOTX64.EFI ::/EFI/BOOT
	mcopy -i $(IMAGE_NAME).hdd@@1M bootloader/limine-binary/BOOTIA32.EFI ::/EFI/BOOT
endif
ifeq ($(ARCH),aarch64)
	mcopy -i $(IMAGE_NAME).hdd@@1M bootloader/limine-binary/BOOTAA64.EFI ::/EFI/BOOT
endif
ifeq ($(ARCH),riscv64)
	mcopy -i $(IMAGE_NAME).hdd@@1M bootloader/limine-binary/BOOTRISCV64.EFI ::/EFI/BOOT
endif
ifeq ($(ARCH),loongarch64)
	mcopy -i $(IMAGE_NAME).hdd@@1M bootloader/limine-binary/BOOTLOONGARCH64.EFI ::/EFI/BOOT
endif

.PHONY: clean
clean:
	$(MAKE) -C kernel clean
	rm -rf iso_root $(IMAGE_NAME).iso $(IMAGE_NAME).hdd

.PHONY: distclean
distclean:
	$(MAKE) -C kernel distclean
	rm -rf iso_root *.iso *.hdd bootloader/limine-binary edk2-ovmf-bins
