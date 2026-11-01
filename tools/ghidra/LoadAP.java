// Map an Actions ATJ2127 .ap overlay file into the current program:
// one block per bank-table entry, MIPS16e ISA mode, functions at SAVE prologues.
// Optional second argument "fileoff:addr,..." maps banks missing from the table
// (each sized up to the next listed offset, max 0x800).
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import ghidra.program.model.lang.Register;
import ghidra.program.model.mem.*;
import java.io.ByteArrayInputStream;
import java.math.BigInteger;
import java.nio.*;
import java.nio.file.*;
import java.util.Arrays;

public class LoadAP extends GhidraScript {
    private byte[] d;
    private Memory mem;
    private AddressSpace sp;
    private Register isa;

    @Override
    public void run() throws Exception {
        d = Files.readAllBytes(Paths.get(getScriptArgs()[0]));
        ByteBuffer bb = ByteBuffer.wrap(d).order(ByteOrder.LITTLE_ENDIAN);
        mem = currentProgram.getMemory();
        for (MemoryBlock b : mem.getBlocks())
            mem.removeBlock(b, monitor);
        sp = currentProgram.getAddressFactory().getDefaultAddressSpace();
        isa = currentProgram.getRegister("ISA_MODE");
        int nfunc = 0;
        int tableLen = bb.getInt(0x2c);
        for (int off = 0x100; off + 12 <= 0x100 + tableLen; off += 12) {
            long fo = bb.getInt(off) & 0xffffffffL;
            long a = bb.getInt(off + 4) & 0xffffffffL;
            long s = bb.getInt(off + 8) & 0xffffffffL;
            if (fo == 0 || a == 0 || s == 0 || s > 0x10000 || fo + s > d.length)
                continue;
            nfunc += mapBank((int) fo, a, (int) s);
        }
        if (getScriptArgs().length > 1) {
            String[] specs = getScriptArgs()[1].split(",");
            for (int k = 0; k < specs.length; k++) {
                String[] fa = specs[k].split(":");
                int fo = Integer.decode(fa[0]);
                int end = k + 1 < specs.length ? Integer.decode(specs[k + 1].split(":")[0]) : fo + 0x800;
                nfunc += mapBank(fo, Long.decode(fa[1]), Math.min(0x800, end - fo));
            }
        }
        Address ea = sp.getAddress((bb.getInt(0x40) & 0xffffffffL) & ~1L);
        addEntryPoint(ea);
        disassemble(ea);
        createFunction(ea, "ap_entry");
        println("LoadAP: blocks=" + mem.getBlocks().length + " prologue functions=" + nfunc + " entry=" + ea);
    }

    private int mapBank(int fo, long a, int s) throws Exception {
        Address start = sp.getAddress(a);
        byte[] chunk = Arrays.copyOfRange(d, fo, fo + s);
        MemoryBlock blk = mem.createInitializedBlock(String.format("b_%08x", a), start,
            new ByteArrayInputStream(chunk), s, monitor, false);
        blk.setRead(true);
        blk.setExecute(true);
        currentProgram.getProgramContext().setValue(isa, start, start.add(s - 1), BigInteger.ONE);
        int nfunc = 0;
        // MIPS16e SAVE (0x6480-0x64ff), optionally preceded by an EXTEND (0xf000-0xf7ff)
        for (int i = 0; i + 1 < chunk.length; i += 2) {
            int hw = (chunk[i] & 0xff) | ((chunk[i + 1] & 0xff) << 8);
            if ((hw & 0xff80) != 0x6480)
                continue;
            int fs = i;
            if (i >= 2) {
                int prev = (chunk[i - 2] & 0xff) | ((chunk[i - 1] & 0xff) << 8);
                if ((prev & 0xf800) == 0xf000)
                    fs = i - 2;
            }
            Address fa = start.add(fs);
            if (getFunctionAt(fa) == null && disassemble(fa) && createFunction(fa, null) != null)
                nfunc++;
        }
        return nfunc;
    }
}
