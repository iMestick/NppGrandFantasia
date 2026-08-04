# NppGrandFantasia

Plugin nativo x64 para Notepad++ destinado a ferramentas de modding e desenvolvimento do Grand Fantasia.


## Atualizacao 0.5.4

- Removido completamente o marcador de background das linhas quebradas.
- Registros invalidos agora recebem uma cor de texto unica em todos os caracteres: ID, pipes e conteudo.
- A cor padrao dos caracteres de erro e vermelha e pode ser alterada em **Cores...**.
- Em registros quebrados, as cores normais dos pipes e o verde do ID ficam suspensos ate o erro ser corrigido.
- Ao corrigir a quantidade de pipes, o registro volta automaticamente para as cores normais.
- A nova configuracao e salva pela chave `BrokenTextColor`; a chave antiga `BrokenLineBackground` ainda e lida para preservar a cor ja escolhida.

## Atualizacao 0.5.3

- IDs numericos de registros validos agora recebem uma cor propria no editor.
- A cor padrao dos IDs validos e verde.
- O botao **Cores...** permite alterar a cor dos IDs validos junto das cores dos pipes e do destaque de erro.
- Somente o numero inicial antes do primeiro pipe e colorido, por exemplo `58904` em `58904|Nome|...`.
- Todo inicio de registro reconhecido no formato `numero|` recebe a cor configurada, inclusive quando o registro estiver com erro de pipes.
- A configuracao e salva em `NppGrandFantasia.ini` pela chave `ValidIdColor`.

## Atualizacao 0.5.2

- O indicador compacto agora mostra somente as linhas quebradas, em ordem crescente.
- Registros que ocupam mais de uma linha aparecem como faixa, por exemplo `42-45`.
- Cada numero ou faixa visivel pode ser clicado para navegar diretamente ate o erro.
- Quando nao ha espaco para mostrar todos os erros, o indicador termina com `...` e o tooltip exibe a lista completa.
- Ao corrigir um registro, a lista da barra e atualizada automaticamente pela validacao em tempo real.
- O botao direito sobre o indicador abre ou oculta o painel completo; o atalho `Ctrl+Q` continua configuravel no Shortcut Mapper.

## Correcao 0.5.1

- Corrigida a compilacao no MSVC 14.51/Visual Studio 18 causada pela mistura entre `int` e `LONG` em `std::max` no posicionamento do indicador da barra de ferramentas.

## Validador de Pipes

O painel **Plugins > NppGrandFantasia > Mostrar/Ocultar Validador de Pipes** analisa o documento ativo em tempo real.

A verificacao so e ativada quando a primeira linha nao vazia possui um cabecalho valido neste formato:

```ini
|V.5|10|
```

Nesse exemplo, cada registro deve possuir exatamente **10 caracteres `|`**. O cabecalho serve apenas como configuracao e nao entra na contagem.

### Regras

- Linhas vazias antes do cabecalho e BOM UTF-8 sao aceitos.
- O cabecalho precisa seguir `|V.versao|QtdPipe|`.
- A versao aceita numeros separados por pontos, como `V.5`, `V.16` ou `V.1.2`.
- `QtdPipe` precisa ser um inteiro maior que zero.
- Cada registro comeca por um ID numerico seguido de pipe: `12345|`.
- Linhas seguintes que nao iniciam outro `ID|` fazem parte do registro atual.
- Todos os pipes do registro, inclusive os das linhas adicionais, sao somados.
- Sem um cabecalho valido, o arquivo e ignorado.
- O painel lista todos os registros quebrados, sem parar no primeiro erro.
- Um duplo clique em um erro leva o cursor ate a linha inicial do registro.


## Indicador compacto na barra de ferramentas

O plugin adiciona um indicador textual no canto direito da barra de ferramentas original do Notepad++. Ele ocupa somente a area vazia disponivel e se oculta automaticamente quando nao ha espaco suficiente.

O indicador mostra as linhas quebradas em ordem:

```text
Linhas: 18, 42-45, 91, 130
```

- Cada numero ou faixa visivel e clicavel e leva diretamente para a linha inicial do erro.
- Registros divididos em varias linhas aparecem como intervalo.
- Quando todos os registros forem corrigidos, o texto muda para `Nenhuma linha quebrada`.
- Se nao houver espaco para exibir tudo, aparece `...`; a lista completa continua disponivel no tooltip e no painel.
- O ponto fica verde quando o arquivo esta valido e usa a cor de erro configurada quando existem quebras.
- A lista e atualizada automaticamente enquanto o arquivo e corrigido.

O botao direito sobre o indicador abre ou oculta o painel completo. O painel tambem pode ser alternado com `Ctrl+Q`.

## Atalho do painel

O comando **Mostrar/Ocultar Validador de Pipes** usa **Ctrl+Q** por padrao. Como o atalho pertence ao comando do plugin, ele aparece no **Shortcut Mapper** do Notepad++ e pode ser alterado ou removido normalmente.

## Interface compacta e acoplavel

- O painel usa o sistema nativo de docking do Notepad++.
- Na primeira abertura, a posicao padrao e o lado direito da janela.
- O painel pode ser redimensionado, movido para outro lado ou deixado flutuante pelo proprio Notepad++.
- A interface foi reduzida para ocupar pouco espaco.
- A validacao continua funcionando mesmo quando o painel esta oculto, permitindo manter somente os indicadores no editor.

## Indicacao visual dos erros

Quando um registro possui pipes a menos ou a mais:

- todos os caracteres do registro recebem a cor de erro, incluindo ID, pipes e texto;
- a cor padrao e vermelha e pode ser alterada em **Cores...**;
- registros com varias linhas recebem a cor em todas as linhas envolvidas;
- o painel mostra linha ou intervalo, ID, quantidade encontrada/esperada e diferenca;
- depois da correcao, o registro volta automaticamente para as cores normais.

## Cores dos pipes

O botao **Cores...** abre a configuracao de realce dos caracteres `|`.

- E possivel usar de 1 a 4 cores.
- Com 1 cor, todos os pipes usam a mesma cor.
- Com 2, 3 ou 4 cores, elas sao aplicadas em sequencia e repetidas ciclicamente.
- A sequencia reinicia em cada linha.
- As configuracoes sao salvas em `NppGrandFantasia.ini`, na pasta de configuracao de plugins do Notepad++.
- Na mesma janela e possivel escolher a cor verde padrao dos IDs validos e a cor dos caracteres dos registros quebrados.
- A cor de erro vem vermelha por padrao e substitui temporariamente as cores do ID e dos pipes no registro invalido.
- O realce so e aplicado quando a validacao estiver ativa por cabecalho ou regra de traducao.
- O ID valido e o numero no inicio do registro, imediatamente antes do primeiro `|`.
- O destaque identifica todo ID reconhecido no formato `^numero|`; quando o registro esta quebrado, todos os seus caracteres usam a cor de erro configuravel.

## Informacoes mostradas

- Linha ou intervalo de linhas do registro.
- ID do registro.
- Quantidade esperada.
- Quantidade encontrada.
- Quantos pipes faltam ou sobram.
- Ultimo ID valido antes da primeira quebra.

## Compilacao

Requisitos:

- Windows 10 ou Windows 11.
- Visual Studio com o workload **Desenvolvimento para Desktop com C++**.
- CMake disponivel no PATH.
- Notepad++ x64.

Execute:

```bat
build.bat
```

A DLL sera gerada normalmente em:

```text
build\Release\NppGrandFantasia.dll
```

Uma estrutura pronta para instalacao tambem sera criada em:

```text
build\Release\plugin\NppGrandFantasia\NppGrandFantasia.dll
```

## Instalacao

Copie a pasta inteira:

```text
build\Release\plugin\NppGrandFantasia
```

para:

```text
C:\Program Files\Notepad++\plugins\
```

O resultado final deve ficar assim:

```text
C:\Program Files\Notepad++\plugins\NppGrandFantasia\NppGrandFantasia.dll
```

Reinicie o Notepad++.

## Desempenho

- Alteracoes sao agrupadas por um debounce de 300 ms.
- O texto e copiado do Scintilla na thread da interface.
- A analise dos registros ocorre em uma thread de trabalho.
- Somente o resultado mais recente do documento ativo e exibido.
- A coloracao dos pipes e dos IDs validos e aplicada apenas nas linhas visiveis e atualizada durante a rolagem.
- Documentos acima de 512 MB sao ignorados para evitar consumo excessivo de memoria.

## Estrutura

```text
NppGrandFantasia/
|-- CMakeLists.txt
|-- build.bat
|-- .gitignore
|-- README.md
|-- examples/
|-- tests/
`-- src/
    |-- PluginMain.cpp
    |-- PipeValidator.cpp/.h
    |-- PipeColorSettings.cpp/.h
    |-- PipeColorDialog.cpp/.h
    |-- ToolbarStatus.cpp/.h
    |-- ValidatorWindow.cpp/.h
    |-- NppGrandFantasia.rc
    |-- resource.h
    `-- npp/
```

## Validador de INIs de traducao

Os INIs de traducao conhecidos sao validados automaticamente pelo nome do arquivo, mesmo sem o cabecalho `|V.x|QtdPipe|`.

Regras:

- A comparacao do nome nao diferencia maiusculas de minusculas.
- O arquivo pode estar em qualquer pasta.
- A quantidade configurada representa o numero exato de caracteres `|` por registro.
- Os registros continuam sendo identificados por `ID|` e podem ocupar mais de uma linha.
- Texto antes do primeiro registro `ID|` e ignorado.
- Quando o nome pertence a lista abaixo, a regra fixa de traducao tem prioridade sobre qualquer cabecalho existente.
- O painel, a cor configuravel dos registros quebrados, a navegacao por duplo clique e as cores dos pipes funcionam da mesma forma que nos INIs com cabecalho.

```text
t_achievement.ini = 3
t_activity.ini = 6
t_battlefield.ini = 2
t_beaststower.ini = 2
t_class.ini = 13
t_collection.ini = 2
t_dialogue.ini = 2
t_dynamicevent.ini = 2
t_elf.ini = 3
t_elfcollect.ini = 3
t_elfking.ini = 3
t_elfracing.ini = 2
t_elftabletability.ini = 2
t_elftabletcombo.ini = 3
t_elfteamfight.ini = 2
t_elftemple.ini = 2
t_elftemplechallenge.ini = 2
t_elftrain.ini = 3
t_enchant.ini = 5
t_equipset.ini = 2
t_exam.ini = 4
t_familytree.ini = 2
t_festival.ini = 2
t_item.ini = 3
t_itemcombo.ini = 2
t_itemmall.ini = 3
t_mentorshipinstance.ini = 3
t_mission.ini = 3
t_monster.ini = 2
t_node.ini = 11
t_npc.ini = 2
t_pkpalacerank.ini = 2
t_pointability.ini = 2
t_race.ini = 2
t_racegroup.ini = 2
t_rainbowevent.ini = 3
t_recommendevents.ini = 4
t_ridecombo.ini = 2
t_spell.ini = 3
t_textindex.ini = 2
t_title.ini = 3
t_vip.ini = 2
```
