// Decompile every function to a text file.
import ghidra.app.decompiler.*;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;
import java.io.PrintWriter;

public class DumpC extends GhidraScript {
    @Override
    public void run() throws Exception {
        DecompInterface di = new DecompInterface();
        di.openProgram(currentProgram);
        int n = 0;
        try (PrintWriter pw = new PrintWriter(getScriptArgs()[0])) {
            for (Function f : currentProgram.getFunctionManager().getFunctions(true)) {
                DecompileResults r = di.decompileFunction(f, 60, monitor);
                pw.println("// ===== " + f.getName() + " @ " + f.getEntryPoint());
                pw.println(r.decompileCompleted() ? r.getDecompiledFunction().getC()
                                                  : "// failed: " + r.getErrorMessage());
                n++;
            }
        }
        println("DumpC: functions=" + n);
    }
}
