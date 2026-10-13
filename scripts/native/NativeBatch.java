// Bounded read-only batch queries. @category ResonantLab
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;

public class NativeBatch extends GhidraScript {
    public void run() throws Exception {
        String[] addresses = getScriptArgs();
        if (addresses.length == 0 || addresses.length > 12)
            throw new IllegalArgumentException("Supply 1..12 function addresses");
        DecompInterface d = new DecompInterface();
        try {
            if (!d.openProgram(currentProgram)) throw new IllegalStateException("Decompiler unavailable");
            for (String address : addresses) {
                if (monitor.isCancelled()) break;
                Function f = currentProgram.getFunctionManager().getFunctionContaining(toAddr(address));
                if (f == null) { println("NATIVE_MISSING " + address); continue; }
                println("NATIVE_BEGIN " + f.getEntryPoint() + " " + f.getSignature());
                Instruction instruction = currentProgram.getListing().getInstructionAt(f.getEntryPoint());
                for (int n = 0; n < 24 && instruction != null && f.getBody().contains(instruction.getAddress()); n++) {
                    StringBuilder bytes = new StringBuilder();
                    for (byte b : instruction.getBytes()) bytes.append(String.format("%02x", b & 255));
                    println("ASM " + instruction.getAddress() + " " + bytes + " " + instruction);
                    instruction = instruction.getNext();
                }
                DecompileResults result = d.decompileFunction(f, 30, monitor);
                if (!result.decompileCompleted()) { println("NATIVE_ERROR " + result.getErrorMessage()); continue; }
                String source = result.getDecompiledFunction().getC();
                println(source.substring(0, Math.min(source.length(), 24000)));
                println("NATIVE_END " + f.getEntryPoint() + " truncated=" + (source.length() > 24000));
            }
        } finally { d.dispose(); }
    }
}
