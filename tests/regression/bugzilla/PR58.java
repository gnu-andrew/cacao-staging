/* tests/regression/bugzilla/PR58.java

   Copyright (C) 2008
   CACAOVM - Verein zur Foerderung der freien virtuellen Maschine CACAO

   This file is part of CACAO.

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2, or (at
   your option) any later version.

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
   02110-1301, USA.

*/


import junit.framework.*;
import junit.textui.*;

import java.io.*;

public class PR58 extends TestCase {
    public static void main(String[] args) {
        TestRunner.run(suite());
    }

    public static Test suite() {
        return new TestSuite(PR58.class);
    }

    class x extends y {}
    class y {}

    public void test() {
        // Delete the class file which is extended.
        new File("PR58$y.class").delete();

        try {
            Class.forName("PR58$x");
            fail("Should throw NoClassDefFoundError");
        }
        catch (ClassNotFoundException error) {
            fail("Unexpected exception: " + error);
        }
        catch (NoClassDefFoundError success) {
            // Check if the cause is correct.
            assertTrue(success.getCause() instanceof ClassNotFoundException);
        }
    }
}
