/*
 * Copyright (c) 2023, Oracle and/or its affiliates. All rights reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.
 *
 * This code is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * version 2 for more details (a copy is included in the LICENSE file that
 * accompanied this code).
 *
 * You should have received a copy of the GNU General Public License version
 * 2 along with this work; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Please contact Oracle, 500 Oracle Parkway, Redwood Shores, CA 94065 USA
 * or visit www.oracle.com if you need additional information or have any
 * questions.
 */

import jdk.test.lib.process.ProcessTools;
import jdk.test.lib.process.OutputAnalyzer;

/*
 * @test TestTableSwitch
 * @bug 8311583
 * @library /test/lib
 * @compile TableSwitchp1.jasm LookupSwitchp1.jasm
 * @run driver TestTableSwitch
 */

public class TestTableSwitch {

    public static void main(String[] args) throws Exception {
        if (args.length == 1) {
            if (args[0].equals("runTable")) {
                TableSwitchp1.runTable();
            } else {  // if (args[0].equals("runLookup"))
                LookupSwitchp1.runLookup();
            }
        } else {
           ProcessBuilder pb = ProcessTools.createTestJvm("TestTableSwitch", "runTable");
           OutputAnalyzer output = new OutputAnalyzer(pb.start());
           output.shouldContain("java.lang.VerifyError: Bad instruction");
           output.shouldHaveExitValue(1);

           pb = ProcessTools.createTestJvm("TestTableSwitch", "runLookup");
           output = new OutputAnalyzer(pb.start());
           output.shouldContain("java.lang.VerifyError: Bad instruction");
           output.shouldHaveExitValue(1);
        }
    }
}


