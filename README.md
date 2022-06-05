# qpoints

QEMU tracing plugin using SimPoints, for generating BBV (Basic Block Vector) for Proxy Kernel with process slicing.

## Compiling

Set `QEMU_DIR` to point to the QEMU source folder before running `make`.

```sh
make QEMU_DIR=/path/to/qemu
```

## Running

```sh
./qemu-system-riscv64 -nographic -machine spike -bios none -d plugin \
    -plugin /path/to/qpoints/libbbv.so,ckpt_start=<checkpoint_func_start>,ckpt_len=<checkpoint_func_len> \
    -kernel /path/to/pk -append "benchmark related arguments"
```

You should see `bbv.gz` after running the above command.

The BBV file is processed by the SimPoints binary to create the simpoints and
weights file:

```sh
/path/to/SimPoint.3.2/bin/simpoint -inputVectorsGzipped -loadFVFile bbv.gz -maxK 10 -saveSimpoints trace.simpts  -saveSimpointWeights trace.weights
```

## Related

* **The original repository** https://github.com/pranith/qpoints/.
* **SimPoint:** http://web.eece.maine.edu/~vweaver/projects/qemusim/.
