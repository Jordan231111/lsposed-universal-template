import java.nio.file.Path;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.jar.JarFile;

/** Keep signing passwords out of command lines and generated source. */
class RunLspatch {
    public static void main(String[] args) throws Exception {
        var options = new ArrayList<>(List.of("-k", args[1],
                System.getenv("TEMPLATE_KS_PASS"), System.getenv("TEMPLATE_KEY_ALIAS"),
                System.getenv("TEMPLATE_KEY_PASS")));
        options.addAll(Arrays.asList(args).subList(2, args.length));
        try (var jar = new JarFile(Path.of(args[0]).toFile())) {
            String main = jar.getManifest().getMainAttributes().getValue("Main-Class");
            Class.forName(main).getMethod("main", String[].class)
                    .invoke(null, (Object) options.toArray(String[]::new));
        }
    }
}
