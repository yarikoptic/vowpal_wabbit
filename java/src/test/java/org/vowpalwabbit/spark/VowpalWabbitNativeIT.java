package org.vowpalwabbit.spark;

import org.junit.Test;
import static org.junit.Assert.*;
import java.io.*;
import java.nio.file.*;
import java.nio.charset.Charset;
import java.nio.charset.StandardCharsets;
import java.util.*;
import org.vowpalwabbit.spark.prediction.*;

/**
 * @author Markus Cozowicz
 */
public class VowpalWabbitNativeIT {
    @Test 
    public void testHashing() throws Exception {
        String w1 = "ஜெய்";

        byte[] sarr = ("a" + w1).getBytes(StandardCharsets.UTF_8);

        int h1 = VowpalWabbitMurmur.hash(sarr, 0, sarr.length, -1801964169);
        int h1n = VowpalWabbitMurmur.hashNative(sarr, 0, sarr.length, -1801964169);

        assertEquals(h1, h1n);
    }

    @Test
    public void testWrappedVsCommandLine() throws Exception {
        String vwBinary = Files.readAllLines(Paths.get(getClass().getResource("/vw-bin.txt").getPath())).get(0);

        // need to use confidence_after_training as otherwise the numbers don't match up...
        Runtime.getRuntime()
            .exec(vwBinary + " --quiet --confidence --confidence_after_training -f target/testSimple1-ref.model -d src/test/resources/test.txt -p target/testSimple1-ref.pred")
            .waitFor();

        byte[] modelRef = Files.readAllBytes(Paths.get("target/testSimple1-ref.model"));
        List<String> predsRef = Files.readAllLines(Paths.get("target/testSimple1-ref.pred"), Charset.defaultCharset());

        byte[] model;
        VowpalWabbitNative vw = null;
        VowpalWabbitExample ex = null;
        FileOutputStream out = null;

        try
        {
            vw = new VowpalWabbitNative("--quiet --confidence --confidence_after_training");
            ex = vw.createExample();

            for (int i=0;i<10;i++) {
                ex.addToNamespaceDense('a',
                    VowpalWabbitMurmur.hash("a", 0),
                    new double[] { 1.0, 2.0, 3.0 });
                ex.setLabel(i % 2);

                ex.learn();

                ScalarPrediction pred = (ScalarPrediction)ex.getPrediction();

                String[] scalarAndConfidenceRef = predsRef.get(i).split(" ");

                // compare predictions and confidence
                assertEquals(Float.parseFloat(scalarAndConfidenceRef[0]), pred.getValue(), 1e-4);
                assertEquals(Float.parseFloat(scalarAndConfidenceRef[1]), pred.getConfidence(), 1e-4);

                ex.clear();
            }

            vw.endPass();

            model = vw.getModel();
            out = new FileOutputStream("target/testSimple1.model");
            out.write(model);
        }
        finally {
            if (out != null)
                out.close();

            if (ex != null)
                ex.close();

            if (vw != null)
                vw.close();
        }

        // compare model
        assertArrayEquals(model, modelRef);
    }

    @Test
    public void testPrediction() throws Exception {
        byte[] model;
        float learnPrediction = 0f;
        VowpalWabbitNative vw = null;
        VowpalWabbitExample ex = null;

        try
        {
            vw = new VowpalWabbitNative("--quiet");
            ex = vw.createExample();
            for (int i=0;i<10;i++) {
                ex.addToNamespaceDense('a',
                    VowpalWabbitMurmur.hash("a", 0),
                    new double[] { 1.0, 2.0, 3.0 });
                ex.setLabel(i % 2);

                ex.learn();
                ex.clear();
            }

            vw.endPass();

            ex.close();

            model = vw.getModel();

            ex = vw.createExample();
            ex.addToNamespaceDense('a',
                VowpalWabbitMurmur.hash("a", 0),
                new double[] { 1.0, 2.0, 3.0 });

            ex.predict();

            ScalarPrediction pred = (ScalarPrediction)ex.getPrediction();
            learnPrediction = pred.getValue();

            assertTrue(learnPrediction > 0);

            vw.close();

            // test the model
            vw = new VowpalWabbitNative("--quiet", model);
            VowpalWabbitArguments args = vw.getArguments();

            assertEquals(18, args.getNumBits());
            assertEquals(0, args.getHashSeed());

            ex = vw.createExample();
            ex.addToNamespaceDense('a',
                VowpalWabbitMurmur.hash("a", 0),
                new double[] { 1.0, 2.0, 3.0 });

            pred = (ScalarPrediction)ex.predict();

            assertEquals(learnPrediction, pred.getValue(), 1e-4);
        }
        finally {
            if (ex != null)
                ex.close();

            if (vw != null)
                vw.close();
        }
    }

    @Test
    public void testBFGS() throws Exception {
        File tempFile = File. createTempFile("vowpalwabbit", ".cache");
        tempFile.deleteOnExit();
        String cachePath = tempFile.getAbsolutePath();
        VowpalWabbitNative vw = null;
        VowpalWabbitExample ex = null;

        try 
        {
            vw = new VowpalWabbitNative("--loss_function=logistic --bfgs --passes 2 -k --cache_file=" + cachePath);
            ex = vw.createExample();

            for (int i=0;i<10;i++) {
                ex.addToNamespaceDense('a',
                    VowpalWabbitMurmur.hash("a", 0),
                    new double[] { 1.0, 2.0, 3.0 });
                ex.setLabel((i % 2) * 2 - 1 );

                ex.learn();
                ex.clear();
            }

            vw.endPass();
            vw.performRemainingPasses();
        }
        finally {
            if (ex != null)
                ex.close();

            if (vw != null)
                vw.close();
        }
    }
}