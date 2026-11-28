# NativeBoy Studio 1.18.0

Ferramenta de desktop para **Windows 10/11 x64** e **Linux x64 (Ubuntu/Lubuntu 24.04 ou mais novo)** que analisa ROMs de Game Boy / Game Boy Color, gera projetos editáveis e **recompila estaticamente** o código SM83 para C++, produzindo executáveis nativos (Windows e Linux) que abrem com dois cliques e não precisam da ROM.

> Use apenas ROMs que você possui legalmente, ou homebrew/software livre. O executável gerado contém o código e os dados do jogo original.

## Instalação no Linux (Ubuntu, Lubuntu, Xubuntu, Kubuntu 24.04+)

Três formatos, escolha um:

- **.deb**: dois cliques em `nativeboy-studio_1.7.0_amd64.deb`, ou `sudo apt install ./nativeboy-studio_1.7.0_amd64.deb`. O apt instala sozinho o `g++`, a `libsdl2-dev` e o `zenity`. O Studio fica no menu de aplicativos (Jogos/Desenvolvimento) e no terminal como `nativeboy-studio`.
- **Portátil (.tar.gz)**: extraia e rode `./NativeBoyStudio` (ou `./instalar.sh` para criar o atalho). Só o `g++` é necessário para converter.
- **AppImage**: `chmod +x NativeBoyStudio-1.7.0-x86_64.AppImage` e dois cliques. Também precisa do `g++` para converter jogos.

Para exportar também a versão **Windows (.exe)** de um jogo a partir do Linux, instale `g++-mingw-w64-x86-64-posix` e use "Exportar para Meus Jogos + versão Windows" (ou `--convert rom.gb --windows` no terminal). O resultado vai para `Meus Jogos/<Nome> (Windows)/`.

Os jogos exportados vão para `~/Documentos/NativeBoy Studio/Meus Jogos/<Nome>/`, com o executável `Nome`, a `libSDL2-2.0.so.0` (carregada da própria pasta) e o atalho `Nome.desktop`. Cada jogo também ganha uma entrada no menu de aplicativos, na seção Jogos. O libstdc++ vai embutido estaticamente, então o jogo roda em qualquer Ubuntu 24.04+ mesmo sem os pacotes de desenvolvimento.

## Para o usuário (Windows)

1. Rode `NativeBoyStudio_Setup.exe`. Ele instala o Studio, o compilador C++ (GCC 15 / w64devkit), o montador RGBDS e o SDK. Não há nada para configurar.
2. Abra o NativeBoy Studio e arraste uma ROM `.gb`/`.gbc` para a janela.
3. Escolha uma das quatro opções:

| Opção | O que faz | Modo |
|---|---|---|
| **JOGAR ROM** | Roda o jogo original no interpretador SM83 de referência. A tela mostra o selo **MODO EMULAÇÃO**. Enquanto você joga, o código executado é registrado para melhorar a conversão. | Emulação |
| **DESCOMPILAR ROM** | Descoberta de código, funções, rotinas de som, gráficos (VRAM e ROM), música (VGM) e uma desmontagem RGBDS que **remonta a ROM idêntica byte a byte**. | Análise |
| **CONVERTER PARA PC NATIVO** | Assistente em 7 passos: tradução estática → C++ → compilação x64 → verificação contra o original → correção automática → **JOGAR NO PC** → exportação. | Nativo |
| **MODIFICAR JOGO** | Editor de tiles (com importação/exportação PNG), paleta padrão, patches IPS, mods, pacotes HD, cheats, ganchos C++ e remontagem a partir do assembly. Backups automáticos. | Editor |

### O relatório de compatibilidade nunca exagera

| Status | Significado |
|---|---|
| **NATIVO COMPLETO (verificado)** | Todo o código executado durante a verificação rodou como x64 compilado, e o estado da máquina (CPU, RAM, VRAM, OAM e imagem) ficou **idêntico ao interpretador de referência em cada quadro**. |
| **NATIVO PARCIAL** | O jogo funciona, mas trechos ainda passaram pelo interpretador de compatibilidade. Use "Corrigir automaticamente". |
| **FALHA** | O código nativo divergiu do original. O executável não deve ser usado. |
| **Ainda não verificado** | Ainda não houve compilação e verificação. |

Caminhos do jogo que a verificação não alcançou continuam protegidos: se forem executados e não estiverem traduzidos, o jogo usa o interpretador de compatibilidade, registra o ponto em `config\missing_code.txt` e você o importa no Studio para recompilar.

### Jogos exportados (`Documentos\NativeBoy Studio\Meus Jogos\<Nome>\`)

```
NomeDoJogo.exe   SDL2.dll   LEIA-ME.txt
assets\  (fontes, mods\, hd\)    config\  (settings.ini, cheats.txt)    saves\
```

Novidades da versão 1.18 (visual clássico, painéis e mais código nativo):
- Dois temas novos, inspirados nos portáteis clássicos, no Studio e nos jogos convertidos:
  - GB clássico: carcaça cinza, painéis com os quatro verdes da tela, letras azul-marinho e detalhes em magenta.
  - GB Color: corpo escuro na cor da carcaça escolhida (uva, morango, azul-petróleo, kiwi, dente-de-leão ou "atômico", transparente), com as cinco cores de destaque.
  - Títulos em fonte pixelada (Press Start 2P e Pixelify Sans, licença SIL OFL, incluídas).
  - Troca em Exibir > Tema, ou em Ferramentas > Geral (com o tamanho da interface).
- Janela do Studio no modelo de programa nativo:
  - Menus no topo (Arquivo, Jogo, Exibir, Ferramentas, Ajuda), com atalhos: Ctrl+O abrir ROM, F5 jogar, F6 descompilar, F7 converter, F8 modificar, Ctrl+L biblioteca, F11 tela cheia, F1 lista de atalhos.
  - Painéis encaixáveis: "Projeto" (o cartucho, as etapas, a natividade e as ações), "Registro de atividades" e a área de trabalho. Arraste as abas para juntar, separar ou mudar de lado; Exibir > Restaurar a disposição dos painéis volta ao padrão.
  - Barra de status com o projeto, a etapa, a porcentagem nativa e o compilador.
  - A janela volta com o tamanho, a posição e o estado (maximizada) que tinha. No Windows 11 a barra de título segue a cor do tema.
  - O jogo emulado roda dentro da área de trabalho, com o depurador em painel próprio.
- Nos jogos convertidos:
  - O tema segue o tipo do jogo (jogo de GB: tema GB; jogo de GB Color: tema GB Color). Dá para trocar em Opções > Acessibilidade > Aparência dos menus, junto com a cor da carcaça.
  - Tela inicial em estilo de cartucho: nome do jogo em letras pixeladas e menu numa telinha, com a setinha piscando na opção escolhida.
- Mais código nativo: o conversor agora segue as chamadas indiretas que os jogos usam para chamar código em outros bancos, sem precisar jogar até lá:
  - "ld hl, Função / call CallHL" (rotina que é só um "jp hl");
  - "ld a, BANCO / ld hl, Função / call FarCall" (troca o banco e chama hl), inclusive com o banco em outro registrador e por "rst";
  - "ld de, Função / push de / ret".
  - trampolins mais elaborados: o banco escolhido por uma sub-rotina (mesmo terminando em "reti"), HL guardado em DE ou na pilha antes do salto;
  - endereços de rotinas guardados para depois ("ld bc, Rotina / call RegistraRotina"), quando ficam logo depois do fim de outra rotina;
  - tabelas de saltos lidas por HL com instruções no meio do caminho.
  Resultado, só com a análise (sem jogar nenhum quadro): no uCity, um jogo completo de GB Color, o código da ROM que caía na emulação em 1.800 quadros passou de cerca de 10 milhões de instruções para zero. O que o jogo copia para a RAM continua precisando da descoberta (jogar), como antes. A ROM de teste far_test.gb (feita para o NativeBoy) converte 100% nativa sem descoberta; antes, 67 instruções caíam na emulação.

Novidades da versão 1.17 (versão para celular Android):
- Cada jogo convertido pode virar um aplicativo Android (.apk), para instalar direto no celular (sem Play Store).
  - Na tela Converter, abra "Celular Android (APK)", crie uma chave de acesso do GitHub (o botão abre a página certa; marque "repo" e "workflow"), cole a chave e clique em "Gerar APK no GitHub".
  - O Studio envia o projeto para um repositório PRIVADO da sua conta, o GitHub compila de graça (uns 10 minutos na primeira vez) e o APK é baixado para "Meus Jogos/Android". Da segunda vez em diante só os arquivos alterados são enviados.
  - O aplicativo é assinado sempre com a mesma chave (criada pelo GitHub na primeira vez e guardada no repositório), então uma versão nova instala por cima da antiga sem perder os saves.
  - Para instalar: copie o APK para o celular e abra; o Android pede para permitir "instalar apps desconhecidos".
  - Também dá para só criar o projeto Android e abrir no Android Studio.
- No celular:
  - Em pé: o console desenhado na tela, com os botões dele funcionando ao toque.
  - Deitado: a imagem do jogo grande, com direcional, A, B, Start, Select, voltar no tempo, acelerar e menu na tela (vários dedos ao mesmo tempo; vibra ao tocar).
  - Tamanho, transparência e vibração dos botões em Opções > Controles; com um controle Bluetooth/USB os botões da tela somem sozinhos.
  - O botão Voltar do celular abre o menu. Ao sair do app (botão de início, uma ligação), o jogo pausa e salva tudo.
  - Menu de opções adaptado para tela estreita, com rolagem pelo dedo.
- Testado num emulador de Android (API 30) no GitHub a cada mudança; não foi testado em celulares reais.

Novidades da versão 1.16 (voltar no tempo, cabo link pela internet, textos, HD, capas e widescreen):
- Voltar no tempo, segunda geração:
  - Linha do tempo no menu (aba Jogo): imagens dos últimos momentos; escolha uma e volte para ela.
  - Som ao contrário enquanto volta (pode desligar), e quanto mais tempo você segura Backspace, mais rápido volta (2x, 4x, 8x), com contador e barra na tela.
  - Quanto guardar: 30 s, 1, 2 ou 5 minutos (Opções > Controles).
  - O cenário lembrado da tela expandida continua ao voltar no tempo.
- Cabo link pela internet:
  - "Abrir a porta no roteador": o jogo pede ao roteador (UPnP) para liberar a porta e mostra o seu endereço na internet. A porta é fechada de novo ao sair. Se a operadora usa endereço compartilhado (CGNAT), o jogo avisa.
  - Código de convite (NB-XXXXX-XXXXX-X) com o endereço, com botão de copiar; o amigo cola o código e conecta.
  - Na mesma rede (Wi-Fi), quem hospeda aparece sozinho na lista do outro jogador.
  - Mostra o atraso da conexão e avisa quando o outro jogador está com outro jogo (ou outra versão).
  - Testado com um roteador simulado; não foi testado com roteadores reais.
- Editor de textos (Modificar > Tradução):
  - "Detectar a tabela sozinho": acha a tabela de letras procurando palavras comuns de jogos, sem você digitar nada (jogos com alfabeto latino).
  - Lista visual: original ao lado do novo texto, com contador de espaço, busca e filtro dos alterados. O arquivo de texto continua disponível.
  - Textos maiores que o original (experimental): vão para um espaço livre do mesmo banco e o ponteiro do jogo é atualizado. Teste as telas depois de aplicar.
- Pacote HD:
  - Uma folha só (folha_hd.png) com todas as peças coletadas, personagens primeiro, para redesenhar num único arquivo; "Importar a folha redesenhada" corta de volta.
  - Modelos já suavizados (xBR) como ponto de partida, e contagem de quantas peças já foram redesenhadas.
- Biblioteca com capas: destaque grande do último jogo jogado, capas no formato de caixa (GB ou GB Color), gênero, ano, editora e jogadores, filtros por gênero e sistema, ordenação e "Trocar capa" para usar uma imagem sua.
- Dados dos jogos: a lista de 4.548 jogos agora tem gênero (3.155), ano (2.335), editora (3.212), desenvolvedora (2.974), número de jogadores (2.575) e série (1.190), do libretro-database (CC BY-SA 4.0). Campos que faltavam numa versão (região, revisão) vêm de outra versão do mesmo jogo. Aparecem no Studio, na biblioteca e em "Sobre" no jogo.
- Widescreen:
  - Correção das listras: o Studio agora sabe, peça por peça do mapa, para qual lugar do mundo o jogo a escreveu, e só mostra nas laterais o que pertence àquele lugar. Em rolagens diagonais (como a abertura do µCity), a regra antiga mostrava 74,7% das laterais certas; a nova, 100% (teste diag_test.gb).
  - Correção: ao atualizar o Studio, projetos convertidos antes eram recompilados só em parte e o jogo podia travar. Agora tudo é recompilado quando o Studio muda.
- Correção: na biblioteca, a ordem "jogados por último" nunca funcionava no Linux.

Novidades da versão 1.15 (visual de console, ligar e desligar, menos atraso e Studio retrô):
- Janela com visual de console (no jogo: Opções > Vídeo > "Janela com visual de console"):
  - A janela vira o aparelho inteiro, em dois estilos desenhados do zero: clássico (inspirado no Game Boy) e Color (inspirado no Game Boy Color). Não usa nomes nem logotipos da Nintendo; o nome no aparelho é "NativeBoy".
  - O jogo aparece na tela do aparelho, com reflexo no vidro (pode desligar).
  - Os botões desenhados afundam quando você aperta no teclado ou no controle, e dá para jogar clicando neles com o mouse ou tocando numa tela sensível ao toque (o direcional aceita diagonais e deslizar o dedo).
  - Luz de ligado (POWER) acesa enquanto o jogo roda.
  - Janela sem bordas: arraste pelo corpo do aparelho, redimensione pelas beiradas. Botões de menu, minimizar e fechar ficam no próprio aparelho, e dois cliques na tela alternam a tela cheia.
  - Oito cores: cinza clássico, roxo translúcido (mostra a placa por dentro), roxo, amarelo, azul-petróleo, vermelho, verde e preto.
  - No Studio, dá para exportar o jogo já com o visual de console (Modificar > Cores e melhorias).
  - Com o visual de console, o widescreen fica desligado (o aparelho tem a tela dele).
- Ligar e desligar como o aparelho (ligado por padrão):
  - Ao abrir, a luz acende, a tela esquenta, o nome NativeBoy desce, aparecem os cinco pontos coloridos e toca um som curto próprio. Qualquer tecla pula.
  - Ao fechar, a imagem some, uma linha clara se fecha e a tela apaga.
- Menos atraso nos controles (run-ahead), em Opções > Controles: o jogo calcula 1 ou 2 quadros à frente antes de mostrar a imagem, então o que você aperta aparece mais cedo do que no aparelho original. No teste, o atraso caiu de 1 quadro para 0, e o jogo, o som e a memória do cenário ficaram exatamente iguais a jogar sem o recurso. Vem em 1 quadro por padrão; usa mais processador.
- Studio com visual "Portátil retrô" (novo padrão): azul-índigo e lilás como os portáteis antigos, botões com relevo, marcações em verde de tela de LCD e fundo com grade de pontos. Os visuais anteriores continuam em Ferramentas > Geral > Aparência do Studio.

Novidades da versão 1.14 (editor de sprites, memória do cenário e janela do jogo):
- Editor de sprites mais fácil (Modificar > Editor de sprites):
  - "Manter minhas mudanças enquanto o jogo roda": o jogo recarrega os gráficos dos personagens o tempo todo (ao andar, pular...). Agora a sua versão volta sempre que o jogo carrega a peça original, então dá para ver o personagem novo se mexendo antes de salvar.
  - "Todas as peças de personagem carregadas": uma folha com todas as peças que o jogo tem na memória de vídeo, para editar poses que não estão na tela. Dá para pegar 1 peça, 2 em pé (8x16) ou 4 (16x16).
  - Espelhar (horizontal e vertical), mover 1 pixel para cada lado, trocar uma cor por outra, refazer.
  - Exportar e importar PNG, para desenhar no Aseprite, Paint, Photoshop etc. Ao importar, as cores são ajustadas sozinhas às 4 do Game Boy, e o transparente vira "vazio".
  - Atalhos com o jogo pausado: 1-4 cor, B lápis, G balde, I conta-gotas, Ctrl+Z desfazer, Ctrl+Y refazer.
  - O editor agora tem duas colunas com rolagem própria e cabe em janelas menores.
- Memória do cenário, segunda geração (tela expandida):
  - Cenário à frente: uma cópia do jogo, separada, roda um pouco no futuro andando na direção em que a câmera vai, e o que ela vê aparece nas laterais antes de você chegar lá. O jogo de verdade não é alterado, e o que você mesmo vê sempre substitui a previsão. No teste, mostrou cerca de duas vezes mais cenário, com 99,8% dos pixels certos.
  - Memória salva: o cenário visto é gravado junto dos saves (a cada 3 minutos e ao fechar). Na próxima vez que você jogar, o jogo reconhece o lugar e a tela expandida já aparece completa. No teste, 100% certo.
  - Personagens e objetos fora da tela original aparecem nas áreas expandidas quando estão mesmo em jogo. Os que o jogo "esconde" fora da tela (estacionados ou teletransportados) continuam escondidos.
  - Cenário lembrado animado: água, tochas e outras peças animadas continuam se mexendo onde o cenário vem da memória.
  - Opções em Opções > Vídeo, todas ligadas por padrão.
- Reconhecer todos os jogos: o Studio vem com uma lista de 4.548 jogos de Game Boy e Game Boy Color (2.375 com tipo de jogo). Ao abrir uma ROM, ela é reconhecida pelo checksum: aparece o nome oficial e, se o jogo não tem perfil próprio, os ajustes de tela do tipo dele. A lista vem do libretro-database (No-Intro), licença CC BY-SA 4.0 (veja licencas/lista_de_jogos.txt). Os tipos vêm do gênero do jogo e não foram conferidos jogo a jogo.
- Janela do jogo exportado:
  - Menu de pausa novo, ocupando a janela inteira, com o jogo escurecido atrás e as seções numa lista à esquerda (Jogo, Vídeo, Áudio...). Cabe em janelas pequenas. LB/RB ou PageUp/PageDown trocam de seção.
  - Tela de abertura com o ícone do jogo e as opções num painel com a faixa colorida.
  - No Windows 11 a barra de título fica roxa, combinando com o resto.
  - "Sobre" mostra o nome oficial do jogo quando ele foi reconhecido; na Biblioteca do Studio, o nome oficial aparece ao passar o mouse na capa.
- Studio: o nome do projeto no topo não fica mais por cima das abas em janelas estreitas.

Novidades da versão 1.13 (visual novo, perfis por jogo e tela expandida):
- Visual novo do Studio: tema roxo translúcido, com botões arredondados e detalhes coloridos. O tema escuro antigo continua em Ferramentas > Geral > Aparência do Studio. O menu dos jogos exportados usa o mesmo visual.
- Perfis automáticos por jogo: ao abrir uma ROM, o Studio reconhece o jogo pelo título do cabeçalho e já começa o projeto com os ajustes daquele tipo de jogo (widescreen, ultrawide, linhas extras, moldura, paleta). O perfil aparece em Modificar > Cores e melhorias, com botão para reaplicar.
  - A lista embutida tem cerca de 30 jogos. Esses perfis seguem o tipo de cada jogo e não foram conferidos com os jogos reais.
  - Para criar ou corrigir perfis, crie o arquivo perfis_jogos.txt na pasta do programa, com linhas no formato TITULO|Nome|tipo|chave=valor;chave=valor. Tipos: plataforma, mapa_livre, rpg, tela_a_tela, tela_fixa, tiro.
- Ultrawide: o jogo passa dos 256 pixels de largura e chega a 352 (21:9 e além). O que fica além do mapa do jogo vem da memória do cenário; o que ainda não é conhecido se desfaz no fundo.
- Expansão vertical: 16 ou 32 linhas a mais acima e abaixo da tela original. O lado que tem barra de status fica fechado, um lado só abre quando há cenário de verdade para mostrar, e em menus a imagem volta ao tamanho original.
  - Com ultrawide ou expansão vertical, o pacote HD, a sombra dos personagens e o bloom ficam desligados.
- Resolução maior: escala "Nítida" (preenche qualquer tela, inclusive 4K, com pixels definidos) e filtro xBR 8x.
- Moldura de console: formato portátil, com direcional e botões desenhados nas laterais, e as cores novas "Roxo translúcido" e "Índigo".
- Correção: na versão portátil e no AppImage, converter um jogo falhava com "X11/Xlib.h" não encontrado em computadores sem o pacote libx11-dev. Os arquivos necessários agora vêm junto.

Novidades da versão 1.12 (instalador por jogo):
- Ao exportar, o Studio também gera um instalador de verdade para cada jogo, em "Meus Jogos/Instaladores".
- No Windows, "Instalar <Jogo>.exe":
  - um assistente com o nome do jogo, a pasta de instalação (com botão Procurar), a opção de atalho na Área de Trabalho, uma barra de progresso e "Abrir o jogo agora";
  - instala para o usuário, sem pedir administrador, em %LOCALAPPDATA%\Programs\<Jogo>;
  - cria atalhos no menu Iniciar e na Área de Trabalho, com o ícone do jogo;
  - aparece em "Aplicativos instalados" do Windows, com nome, ícone, versão e tamanho;
  - o desinstalador pergunta se você quer manter os saves e as capturas;
  - instalar uma versão nova atualiza o jogo na mesma pasta e mantém as configurações do jogador.
- No Linux, um pacote .deb (Ubuntu, Lubuntu, Mint, Debian...):
  - instala o jogo em /opt/nativeboy-jogos, com ícone no menu de aplicativos e o comando nativeboy-<jogo>;
  - os saves ficam em ~/.local/share/nativeboy/jogos/<jogo>;
  - é gerado pelo próprio Studio, sem programas extras;
  - remover o pacote apaga o jogo, mas não os saves.
- No Linux, "Exportar + versão Windows" gera os dois instaladores.
- Para não gerar o instalador, desmarque "Gerar também um instalador", ao lado do botão Exportar.

Novidades da versão 1.11 (widescreen: o lado para onde você anda):
- Lembrar lugares já visitados: quando você sai de um lugar (entra numa casa, abre um menu, muda de tela) e depois volta, o jogo reconhece o lugar comparando a tela com os que já viu. As laterais voltam a mostrar o cenário de antes. No teste, a lateral ficou 100% igual à rua, contra 47% sem esse recurso. Cenários que se repetem são resolvidos pela posição em que você saiu.
- O lado ainda não desenhado agora some suavemente: o lado para onde você anda ainda não foi desenhado pelo jogo. Em vez de uma faixa lisa, ele se desfaz num degradê no fundo escolhido (desfocado, imagem etc.), mesmo quando a imagem ocupa a tela inteira.
- Detecção melhor de restos de outras telas:
  - colunas que o jogo limpou logo depois de saírem da tela, e que reaparecem à frente quando o mapa dá a volta;
  - tudo o que é de antes de a tela apagar (troca de cena).
- Novas opções no jogo, em Opções > Vídeo, as duas ligadas por padrão: "Lembrar lugares já visitados" e "Esconder suavemente o que o jogo ainda não desenhou".

Novidades da versão 1.10 (executável único):
- Ao exportar, o Studio cria também o jogo em um arquivo só, em "Meus Jogos/Executável único":
  - no Windows, Jogo.exe;
  - no Linux, Jogo.x86_64.
  Programa, SDL2, ícone, abertura, músicas, conquistas e configurações do criador vão dentro dele, comprimidos.
- É só mandar o arquivo para alguém: dois cliques e o jogo abre. Não precisa de instalador nem de pasta.
- Na primeira vez, o jogo se instala sozinho na pasta de dados do usuário:
  - Windows: %LOCALAPPDATA%\NativeBoy\Jogos\<jogo>;
  - Linux: ~/.local/share/nativeboy/jogos/<jogo>.
  Os saves ficam lá. Nas próximas vezes o jogo abre direto, sem instalar de novo.
- Uma versão nova do mesmo arquivo atualiza o jogo e mantém os saves e as configurações do jogador.
- Modo pendrive: crie ao lado do arquivo uma pasta "<nome do arquivo> - dados" (por exemplo "Microcity - dados"). O jogo passa a guardar tudo nela, sem tocar no PC.
- No Linux, o Studio gera os dois arquivos com "Exportar + versão Windows".
- Pode ser desligado no Studio, com a opção "Gerar também um executável único", ao lado do botão Exportar.

Novidades da versão 1.9 (widescreen inteligente):
- O jogo agora avalia a cada quadro se as laterais do widescreen mostram cenário de verdade.
- Em menus, telas de pausa, janelas que cobrem a tela inteira e cenas redesenhadas só na área original, as duas laterais teriam apenas restos de outras telas. Nesses momentos a imagem volta suavemente para a largura original (160 pixels), e o fundo escolhido (desfocado, moldura etc.) preenche o resto.
- Quando o jogo continua, a imagem se abre de novo em widescreen. A volta só acontece depois de a cena ficar estável por cerca de 2/3 de segundo, para não ficar abrindo e fechando.
- Ao rolar o cenário, só um dos lados fica sem conteúdo (o que ainda não apareceu), por isso a rolagem não fecha o widescreen.
- Vem ligado por padrão. Dá para desligar em Opções > Vídeo > "Widescreen inteligente" no jogo, ou definir o padrão de cada projeto no Studio.

Novidades da versão 1.8 (biblioteca e atalhos):
- Aba "Biblioteca" no Studio, no estilo de uma loja de jogos de PC: todos os jogos de "Meus Jogos" em cartões com capa (tela de abertura ou ícone do jogo), tempo jogado e conquistas desbloqueadas. Os jogos jogados por último aparecem primeiro, e dá para procurar pelo nome.
- Cada cartão tem botões para jogar direto, criar atalhos e abrir a pasta. A versão para Windows de um jogo aparece marcada quando você está no Linux (e vice-versa).
- Ao exportar, o Studio cria sozinho um atalho com o ícone do jogo:
  - no Windows, na Área de Trabalho e no menu Iniciar, em "Jogos NativeBoy";
  - no Linux, na Área de Trabalho e no menu de aplicativos.
  Dá para desligar isso em Ferramentas > Geral.
- Cada jogo exportado ganha o arquivo config/jogo.ini (nome, executável, plataforma e data), que a biblioteca usa. Jogos exportados por versões antigas também aparecem.

Novidades da versão 1.7 (widescreen que estende o jogo):

- **Memória do cenário:** o que já passou pela tela é lembrado em coordenadas do "mundo" do jogo e reaparece nas laterais quando o mapa não tem mais aquela parte (coluna antiga, ou reaproveitada pelo jogo depois que você viu). As cores acompanham fades e trocas de paleta. Mudanças de tela, telas apagadas e saltos de câmera limpam a memória.
- **Barras de status na largura toda:** o HUD feito com a janela do Game Boy continua até as bordas repetindo a coluna da ponta, então linhas e molduras seguem retas. HUDs feitos com o fundo e rolagem própria também são reconhecidos e estendidos.
- **Sprites:** objetos guardados fora da tela (coluna 0 ou depois da 168) continuam escondidos, e há uma opção para mostrá-los.

Novidades da versão 1.6 (janela de aplicativo de verdade):

- **Título da janela:** só o nome do jogo, com "(pausado)" quando parado. No Windows 10/11 a barra de título fica escura; no Linux a janela fica agrupada com o atalho na barra de tarefas.
- **A janela lembra tamanho, posição e se estava maximizada.** Dois cliques na imagem alternam a tela cheia.
- **Pausa e silencia sozinho** quando a janela vai para segundo plano ou é minimizada, como os jogos de PC.
- **O mouse some enquanto você joga** e volta ao mexer.
- **Barra de atalhos no topo** (Menu, Pausar, Salvar, Carregar, Captura, Gravar vídeo, Tela cheia) ao passar o mouse.
- **Opção "Janela sempre por cima".**
- **.exe com informações do jogo:** nome e versão aparecem nas Propriedades do arquivo e no Gerenciador de Tarefas.

Novidades da versão 1.5 (menos emulação, gravar e compartilhar):

- **Exploração automática:** depois de jogar sozinho, o Studio volta a momentos salvos e tenta outros jeitos de jogar, guardando os momentos que revelam código novo. Mais código descoberto significa menos trechos emulados no executável. É ligada por padrão e pode ser desligada em Converter.
- **Tabelas de saltos lidas por HL** (`ld hl,Tabela / ... / jp hl`) são reconhecidas na análise estática, então o código delas vira nativo mesmo sem ter sido executado na descoberta.
- **Gravar GIF (F6) e vídeo com som (F7):** o vídeo é .avi (Motion-JPEG + áudio PCM), que abre no Windows, no VLC e em editores. O GIF é animado, com até 1 minuto.
- **Galeria de capturas no menu do jogo:** miniaturas das fotos e GIFs, com vídeos e áudios na mesma lista, além dos botões Abrir e Apagar.

Novidades da versão 1.4 (cara de jogo de PC e gráficos modernos):

- **Tela inicial:** o jogo abre com o título e as opções Continuar (com a data), Novo jogo, Carregar estado salvo, Opções e Sair, e mostra o tempo de jogo e as conquistas. Funciona com mouse, teclado e controle.
- **Conquistas:** o criador define no Studio (Modificar > Conquistas) condições na memória do jogo. O jogador recebe um aviso no canto da tela ao desbloquear e vê a lista no menu. Elas não contam com trapaças ativas.
- **Tela cheia como em jogo de PC:** sem bordas, ou exclusiva com a resolução escolhida.
- **Filtros xBR 2x e 4x:** curvas e diagonais arredondadas em alta definição, em vez de degraus.
- **Efeitos modernos:** sombra sob os personagens, cores vivas, vinheta e brilho das luzes (bloom), ajustáveis no jogo e com padrão definido pelo criador.

Novidades da versão 1.3:

- **Voltar no tempo e acelerar:** segure Backspace (ou o gatilho esquerdo) para rebobinar até 60 s; Tab (ou o gatilho direito) acelera de 2x a 10x.
- **Estados salvos com miniatura:** 8 espaços com foto e data, salvamento automático ao fechar e a pergunta "continuar de onde parei?" ao abrir.
- **Menu pelo controle:** abre com Guia/Home ou Select+Start, navega com o direcional, A confirma, B volta, LB/RB trocam de aba, Start fecha.
- **Turbo e acessibilidade:** botões Turbo A/B com velocidade ajustável, correção para daltonismo (protanopia, deuteranopia, tritanopia), paleta acessível e tamanho do menu de 100% a 200%.
- **Trilha remasterizada:** troque as músicas do jogo por arquivos OGG (Studio > Modificar > Trilha sonora). O jogador grava o áudio em WAV pelo menu.
- **Ícone e tela de abertura por jogo:** ícone embutido no .exe e na janela, gerado sozinho a partir da tela do jogo ou escolhido pelo criador, e imagem de abertura opcional.
- **Tradução:** descobre a tabela de caracteres pela busca relativa, extrai os textos para um arquivo editável e reinsere, gerando um mod IPS (Studio > Modificar > Tradução).
- **Depurador:** pausar, executar instrução por instrução, pontos de parada, registradores, memória, tiles da VRAM e sprites (Studio > Jogar ROM > Depurador).
- **Buscador de trapaças:** procura na memória o valor de vidas, dinheiro ou energia e trava (menu do jogo > Trapaças).

Novidades da versão 1.2:

- **Super Game Boy:** jogos com suporte a SGB recebem as cores de cada tela e a moldura feitas pelo próprio jogo (pacotes PAL, ATTR, transferências de borda e de paleta, modo multiplayer da detecção). Liga e desliga no menu do jogo e no Studio.
- **Laterais da tela:** quando a janela é maior ou mais larga que o jogo, as sobras podem mostrar luz ambiente (a imagem do jogo desfocada), as bordas do jogo estendidas, uma moldura de console desenhada (6 cores), uma imagem de capa (PNG escolhido pelo jogador ou pelo criador no Studio) ou preto.
- **Widescreen adaptativo:** a largura da imagem acompanha o formato da janela (de 160 a 256 pixels), sem faixas laterais.
- **Widescreen melhorado:** colunas do mapa que o jogo deixou com restos de outras telas são escondidas, com opção de esmaecer as bordas.
- **Descompilação:** importação de arquivos de símbolos `.sym` (RGBDS/BGB/no$gmb; um `.sym` ao lado da ROM é importado sozinho) com nomes de funções, rótulos e variáveis na desmontagem, no C++ gerado e no pseudocódigo; registradores de hardware com nomes (`rLCDC`, `rLY`...); reconhecimento de **tabelas de saltos** (`rst`/`call` para uma rotina despachante seguida de ponteiros), que viram `dw Rotulo`; visão **pseudocódigo C** legível (`desmontagem/pseudocodigo.c` e aba no Studio). A remontagem continua idêntica byte a byte.
- **Cartuchos:** MBC7 (acelerômetro com analógico direito ou I/J/K/L, EEPROM 93LC56), HuC1, HuC3 (relógio básico), vibração do MBC5 no controle.
- **Cabo link pela rede (TCP):** dois jogadores, um hospeda e o outro conecta pelo IP (porta 5738).
- **Precisão:** temporização de memória por ciclo-M, bug do HALT, PPU com mudanças de registradores no meio da linha, APU com síntese band-limited.
- **Distribuição:** AppImage, exportação de .exe a partir do Linux, verificação de atualizações pelos Releases do GitHub, pacote de diagnóstico (.zip sem a ROM) e SHA256SUMS.

Melhorias da versão 1.1: **widescreen 16:9** (256×144, experimental), **sem limite de sprites** (menos piscadas), **colorização de jogos de GB** (paletas separadas para cenário e personagens, 9 predefinidas + editor de paleta personalizada no Studio), **rastro do LCD** e filtro **TV CRT**. O criador escolhe os padrões em Modificar > Cores e melhorias.

Camada moderna, toda opcional (o **modo fidelidade** desliga tudo de uma vez): teclado e controles com remapeamento, tela cheia/janela, escala inteira/ajustada/esticada, filtros (nítido, suave, Scale2x, Scale3x, grade LCD), V-Sync, volume e canais de áudio, saves de bateria automáticos, 4 espaços de save state, capturas PNG (F12), aceleração (Tab), paletas DMG, correção de cor GBC, mods (IPS, cheats GameShark, HD), pacotes HD por tile e menu completo (Esc/F1).

## Arquitetura

```
core/      Máquina GB/GBC compartilhada: memória, MBC1/2/3(+RTC)/5/7, HuC1/HuC3, PPU DMG+CGB, APU, SGB, timer, HDMA
           optable.cpp = ÚNICA tabela de opcodes (tamanho, ciclos, texto RGBDS, semântica C++)
           interp_gen.inc = interpretador GERADO dessa tabela (tools/gen_interp.cpp)
recomp/    analysis (descida recursiva + rastros + código em RAM + tabelas de saltos + símbolos), codegen (SM83 → C++), disasm (RGBDS + pseudocódigo C)
runtime/   main.cpp do jogo exportado + selftest (nativo × interpretador, quadro a quadro)
common/    plataforma Win32/Linux, configurações, vídeo/filtros/HD, GamePlayer (camada moderna), cabo link TCP, UI
studio/    aplicativo (SDL2 + Dear ImGui), projetos, pipeline, compilador integrado, trabalhos em segundo plano
installer/ NSIS (instalador x64), textos dos pacotes Linux
tests/     run_all.sh → RESULTADOS.md, gui_test.sh (interface via Xvfb), roms/ (ROMs de teste próprias: SGB, MBC7, link, widescreen, tabela de saltos)
```

Como o recompilador funciona:

- **Descoberta de código.** Descida recursiva a partir dos vetores de reset e de interrupção, com rastreamento de troca de banco (`ld a,n / ld [$2000],a`). A isso se somam os rastros de uma sessão automática (e das suas sessões de jogo) e os logs de código faltante.
- **Tradução.** Cada instrução vira um bloco C++ com rótulo. Blocos de 1 KB por banco viram funções com `switch(pc)`. Saltos diretos locais viram `goto`, e saltos indiretos (`ret`, `jp hl`, interrupções, troca de banco) voltam a um despachante nativo. A pilha emulada é preservada, então truques com o endereço de retorno funcionam.
- **Mesma semântica nos dois modos.** O interpretador e o código gerado usam o mesmo texto C++ por opcode, saído de `optable.cpp`, e por isso não divergem. A verificação confirma isso a cada quadro.
- **Código copiado para a RAM** (rotina de DMA, testes da Blargg). É traduzido com verificação dos próprios bytes em cada instrução e suporta várias versões diferentes no mesmo endereço.
- **Hardware sincronizado sob demanda (v1.1).** O código traduzido só soma ciclos (`m.tick` é uma soma e uma comparação, inline). Vídeo, som e timers só são atualizados quando o jogo acessa o hardware ou quando chega o próximo evento (fim de modo de vídeo, estouro de timer, fim de quadro), e o HALT pula direto ao próximo evento. O hash do estado completo foi comparado quadro a quadro com o núcleo anterior em 6 ROMs: idêntico. O µCity roda 81× mais rápido que o tempo real, e a CPU custa menos de 5% do tempo. O resto é a simulação do vídeo e do som.
- **Dados do jogo.** São embutidos no `.exe` via `.incbin`, e por isso a ROM não é necessária.

## Testes verificáveis

`tests/run_all.sh <pasta_de_roms> <pasta_rgbds>` gera `tests/RESULTADOS.md`. Resultado atual: **73 aprovados, 0 reprovados**.

- Interpretador: os 11 testes Blargg `cpu_instrs`, o combinado, `instr_timing`, `mem_timing`, `mem_timing-2`, `halt_bug` e `interrupt_time`, todos "Passed".
- Nativo: os mesmos ROMs recompilados ficam idênticos ao interpretador em todos os quadros, com 0 instruções interpretadas.
- dmg-acid2 e cgb-acid2: nativo idêntico ao interpretador. µCity (jogo GBC real, MBC5): 100% nativo e idêntico depois da correção automática.
- ROMs de teste próprias (em `tests/roms`, com o código-fonte): Super Game Boy, MBC7, cabo link (dois Game Boys por TCP local), widescreen, tabela de saltos com `.sym`, trilha remasterizada e tradução.
- Voltar no tempo (estados restaurados exatos), depurador (ponto de parada e passo a passo), tradução de ponta a ponta e troca de música por OGG.
- Desmontagem: todas as ROMs remontadas pelo RGBDS idênticas byte a byte, inclusive com símbolos e tabelas.
- Interface: o Studio Linux é aberto num X virtual com uma ROM, desenha a tela, recebe teclado e fecha sem erro.
- Informativo: Mealybug Tearoom (mudanças de registradores no meio da linha) 87,7% dos pixels iguais às fotos do hardware real.

## Compilar a partir do código-fonte

Dependências fixadas: `scripts/fetch_deps.sh` baixa tudo para `deps/`. O workflow `.github/workflows/build.yml` roda os testes, gera todos os pacotes e, numa tag `v*`, publica o Release.

Linux: `sudo apt install g++ libsdl2-dev imagemagick dpkg-dev`, depois `./build_linux.sh` → `.deb`, `.tar.gz`, `.AppImage` e `SHA256SUMS` em `dist/linux/`.

Windows (compilação cruzada a partir do Linux):

Dependências: `mingw-w64`, `nsis`, `imagemagick`; SDL2 2.30 (mingw devel), Dear ImGui, stb, w64devkit 2.4.0 e RGBDS 0.8.0 (win64) em `$DEPS`.

```
./build_host.sh          # ferramentas de teste no Linux
DEPS=/caminho/deps ./build_win.sh   # NativeBoyStudio.exe + dist/ + NativeBoyStudio_Setup.exe
```

## Limitações conhecidas

- **Cartuchos:** ainda não há MBC6, Game Boy Camera, TAMA5 nem MMM01; o Studio avisa ao abrir esses cartuchos. O MBC7 e o HuC3 foram implementados pela documentação e conferidos com ROMs de teste próprias, não com os jogos comerciais. O infravermelho do HuC1/HuC3 responde "sem sinal".
- **Precisão.** A verificação compara o código nativo com o interpretador do próprio NativeBoy, não com um Game Boy real. A suíte Mooneye não está incluída. O vídeo acerta 87,7% dos pixels nos testes Mealybug (1 de 24 idêntico), e 3 dos 12 testes de som `dmg_sound` (truques da RAM de onda) ainda falham.
- **Cobertura.** "NATIVO COMPLETO" vale para o que a verificação executou. Áreas não alcançadas usam o fallback registrado, como explicado acima.
- **Cabo link.** Funciona bem em rede local. Pela internet, cada byte espera a resposta do outro lado, então a latência deixa trocas longas lentas. Quem hospeda precisa liberar a porta TCP 5738.
- **O que foi testado.** No Windows, o Studio, os jogos exportados e o instalador foram testados via Wine, sem áudio real nem controle físico. Ainda falta testar num Windows 10/11 de verdade e com jogos comerciais. O instalador não tem assinatura digital, então o SmartScreen vai avisar; assinar exige um certificado de assinatura de código em nome do autor.
- **Atualizações.** Informe o repositório do GitHub (`dono/repositório`) ou a URL de um canal JSON em Ferramentas > Atualizações.

Licença: MIT (veja `installer/LICENCA.txt`). Componentes de terceiros: SDL2 (zlib), Dear ImGui (MIT, ramo docking), stb, w64devkit/GCC, RGBDS (MIT), DejaVu, Press Start 2P e Pixelify Sans (SIL Open Font License 1.1).
