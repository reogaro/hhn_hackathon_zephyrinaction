import serial, subprocess, time, threading

ser1 = serial.Serial('/dev/ttyUSB1', 115200, timeout=0.1)
stop = False
def reader(s, name):
    while not stop:
        d = s.read(1024)
        if d:
            print(f'[{name}]', d.decode('latin1', errors='replace'), end='', flush=True)

t1 = threading.Thread(target=reader, args=(ser1, 'USB1'))
t1.start()

cmd = [
    'riscv64-zephyr-elf-gdb', 'build/tilt_maze/zephyr/zephyr.elf', '--batch',
    '-ex', 'set pagination off',
    '-ex', 'set confirm off',
    '-ex', 'target extended-remote localhost:3333',
    '-ex', 'thread 2', '-ex', 'load',
    '-ex', 'set *(uint32_t*)0x02000000 = 0',
    '-ex', 'set *(uint32_t*)0x02000004 = 0',
    '-ex', 'set *(uint32_t*)0x02000008 = 0',
    '-ex', 'set *(uint32_t*)0x0200000c = 0',
    '-ex', 'set *(uint32_t*)0x02000010 = 0',
    '-ex', 'thread 3', '-ex', 'set $mstatus = 0', '-ex', 'set $mie = 0', '-ex', 'set $mip = 0', '-ex', 'set $mscratch = 0', '-ex', 'set $pc = 0x80000000',
    '-ex', 'thread 4', '-ex', 'set $mstatus = 0', '-ex', 'set $mie = 0', '-ex', 'set $mip = 0', '-ex', 'set $mscratch = 0', '-ex', 'set $pc = 0x80000000',
    '-ex', 'thread 5', '-ex', 'set $mstatus = 0', '-ex', 'set $mie = 0', '-ex', 'set $mip = 0', '-ex', 'set $mscratch = 0', '-ex', 'set $pc = 0x80000000',
    '-ex', 'thread 2', '-ex', 'set $mstatus = 0', '-ex', 'set $mie = 0', '-ex', 'set $mip = 0', '-ex', 'set $mscratch = 0', '-ex', 'set $pc = 0x80000000',
    '-ex', 'continue'
]
print("Running GDB...")
p = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE)

for _ in range(15):
    if p.poll() is not None:
        print("GDB exited early.")
        break
    time.sleep(1)

print("Sending 'kernel uptime' to console...")
ser1.write(b'\r\nkernel uptime\r\n')
time.sleep(2)
p.kill()
stop = True
t1.join()
