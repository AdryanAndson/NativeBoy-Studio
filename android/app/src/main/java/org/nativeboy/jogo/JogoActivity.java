package org.nativeboy.jogo;

import org.libsdl.app.SDLActivity;

// A tela do jogo: o SDL cuida da janela, do som, do toque e dos controles; o jogo nativo está em libmain.so
public class JogoActivity extends SDLActivity {
    @Override
    protected String[] getLibraries() {
        return new String[] { "SDL2", "main" };
    }
}
