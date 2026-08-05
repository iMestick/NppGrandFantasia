# NppGrandFantasia 0.8.2

Plugin nativo x64 para Notepad++ com ferramentas para trabalhar nos arquivos INI do Grand Fantasia.

## Recursos atuais

- Validação de pipes por cabeçalho `|V.x|QtdPipe|`.
- Regras fixas para os INIs de tradução `t_*.ini` cadastrados.
- Lista compacta e clicável das linhas quebradas.
- Coloração configurável de pipes, IDs válidos e registros quebrados.
- Paleta `$1$` até `$72$` baseada nas cores do jogo.
- Painel completo pelo atalho `Ctrl+Q`, configurável no Shortcut Mapper.
- Indicadores compactos na barra superior do Notepad++.
- Vinculação e sincronização segura de arquivos espelhados `S_ -> C_`.

## Vínculos S_ -> C_

Ao clicar no bloco de vínculo da barra superior, o plugin mostra todos os pares correspondentes que estão abertos:

```text
[✓] S_Item.ini       C_Item.ini       Ativo
[✓] S_Enchant.ini    C_Enchant.ini    Ativo
[ ] S_Spell.ini      C_Spell.ini      Disponível
```

Cada checkbox é independente. Vários pares diferentes podem permanecer ativos ao mesmo tempo.

Regras:

- O principal deve começar com `S_`.
- O espelho deve começar com `C_`.
- O restante do nome e a extensão devem corresponder.
- `S_` sempre envia para `C_`.
- `C_` nunca envia alterações para `S_`.
- Um mesmo documento não pode participar de dois vínculos conflitantes.
- O `C_` fica protegido contra edição manual enquanto o vínculo estiver ativo.
- Ao desvincular, o estado somente leitura anterior é restaurado.

## Sincronização

A criação do vínculo compara o conteúdo completo. Havendo diferença, o documento `C_` recebe uma cópia integral do `S_`.

A sincronização inclui:

- conteúdo multilinha;
- pipes;
- espaços;
- linhas vazias;
- caracteres especiais;
- CRLF, LF ou CR presentes no conteúdo.

Durante a digitação é usado um debounce curto. No salvamento, a sincronização é imediata.

Cada vínculo possui fila, geração e estado próprios. Uma sincronização nova substitui apenas o trabalho antigo do mesmo par, sem descartar trabalhos de outros pares.

## Salvamento pelo Notepad++

Ao salvar um arquivo `S_` com `Ctrl+S` ou pelo botão nativo:

1. O plugin sincroniza `S_ -> C_` antes do salvamento do principal.
2. O Notepad++ salva normalmente o `S_`.
3. O `C_` é registrado no estado interno **não salvo** do Notepad++ com `NPPM_MAKECURRENTBUFFERDIRTY`.
4. Após o término do save do principal, o `C_` correspondente entra na fila de salvamento.
5. O plugin usa primeiro `NPPM_SAVEFILE`, a API nativa para salvar um arquivo aberto pelo caminho completo.
6. Caso o buffer continue modificado, usa como fallback o comando nativo `NPPM_SAVECURRENTFILE` no `BufferID` exato.
7. A view, as abas ativas, o foco, o cursor, as seleções, o scroll e o zoom anteriores são restaurados imediatamente.

A marcação de dirty state ocorre somente uma vez enquanto o `C_` permanecer não salvo. Quando a última edição ainda está aguardando o debounce no momento do `Ctrl+S`, essa marcação é postada para depois de `NPPN_FILEBEFORESAVE`, evitando reentrada durante o save do `S_`.

O plugin usa o savepoint real do documento para confirmar o resultado, evitando mostrar falha quando o Notepad++ salvou o arquivo, mas retornou um valor inconsistente.

O botão **Sync** sincroniza e salva todos os vínculos ativos.

## Segurança de encoding

O encoding é capturado separadamente para cada `S_` e cada `C_` no momento do vínculo.

- O modo de encoding do `C_` nunca é substituído pelo modo do `S_`.
- BOM não é inserido nem removido pelo plugin.
- UTF-8, UTF-16, ANSI, Big5 e outras code pages legadas permanecem sob o buffer original do Notepad++.
- Para code pages legadas, a conversão usa validação estrita e round-trip.
- Se qualquer caractere não puder ser representado no encoding do `C_`, o conteúdo anterior é mantido intacto.
- Após salvar, o plugin confirma novamente o modo de encoding e a code page interna do documento.
- Mudanças manuais de encoding durante um vínculo encerram o vínculo por segurança.

## Estabilidade visual

- O painel `Ctrl+Q` inicia oculto.
- Os indicadores compactos continuam visíveis.
- Controles da barra não são destruídos nem recriados durante a digitação.
- Sincronizações não recalculam o layout.
- Acesso temporário a documentos preserva a view ativa, as abas, o foco, o cursor, as seleções e a rolagem relevante.
- Ativações internas usadas para registrar o estado não salvo ou para o fallback de salvamento ficam com redraw suspenso e não disparam nova sincronização.
- O dirty state do `C_` é registrado no máximo uma vez por ciclo de salvamento, evitando troca interna repetida de abas durante a digitação.
- `WM_SETREDRAW` é aplicado somente às views que já estavam visíveis; a sub-view oculta nunca é revelada pela sincronização.
- Nenhum documento é ativado internamente em uma view oculta.
- Os Scintillas auxiliares ficam em um host privado invisível, fora do layout da janela principal do Notepad++.

## Build

Requisitos:

- Windows 10 ou 11;
- Visual Studio com MSVC para x64;
- CMake 3.20 ou superior.

Execute:

```bat
build.bat
```

Artefatos finais:

```text
build\Release\NppGrandFantasia.dll
build\Release\NppGrandFantasia.lib
build\Release\NppGrandFantasia.pdb
```

O script remove cópias antigas da DLL em outras pastas de `build` e exclui o arquivo `.exp` após a compilação.

## Instalação

Crie a pasta:

```text
Notepad++\plugins\NppGrandFantasia\
```

Copie para ela:

```text
NppGrandFantasia.dll
```

Reinicie o Notepad++.

## Logs

As operações de vínculo, encoding, sincronização e salvamento são registradas em:

```text
NppGrandFantasia_mirror.log
```
