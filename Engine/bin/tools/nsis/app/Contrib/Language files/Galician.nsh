;Language: Galician (1110)
;Ramon Flores <fa2ramon@usc.es>

!insertmacro LANGFILE "Galician" "Galego"

!ifdef MUI_WELCOMEPAGE
  ${LangFileString} MUI_TEXT_WELCOME_INFO_TITLE "Benvindo ao Asistente de Instalaci?n do $(^NameDA)"
  ${LangFileString} MUI_TEXT_WELCOME_INFO_TEXT "Este asistente vai axud?-lo durante a instalaci?n do $(^NameDA).$\r$\n$\r$\nRecomenda-se fechar todas as outras aplicaci?ns antes de iniciar a instalaci?n. Isto posibilita actualizar os ficheiros do sistema relevantes sen ter que reiniciar o computador.$\r$\n$\r$\n$_CLICK"
!endif

!ifdef MUI_UNWELCOMEPAGE
  ${LangFileString} MUI_UNTEXT_WELCOME_INFO_TITLE "Benvindo ao Asistente de desinstalaci?n do $(^NameDA)"
  ${LangFileString} MUI_UNTEXT_WELCOME_INFO_TEXT "Este asistente vai axud?-lo durante a desinstalaci?n do $(^NameDA).$\r$\n$\r$\nAntes de iniciar a desinstalaci?n, certifique-se de que o $(^NameDA) non est? a executar-se.$\r$\n$\r$\n$_CLICK"
!endif

!ifdef MUI_LICENSEPAGE
  ${LangFileString} MUI_TEXT_LICENSE_TITLE "Contrato de licenza"
  ${LangFileString} MUI_TEXT_LICENSE_SUBTITLE "Por favor, verifique os termos da licenza antes de instalar o $(^NameDA)."
  ${LangFileString} MUI_INNERTEXT_LICENSE_BOTTOM "Se aceitar os termos da licenza, clique en 'Aceito' para continuar. Cumpre aceitar o contrato para instalar o $(^NameDA)."
  ${LangFileString} MUI_INNERTEXT_LICENSE_BOTTOM_CHECKBOX "Se aceitar os termos da licenza, clique na caixa de selecci?n abaixo. Cumpre aceitar o contrato para instalar o $(^NameDA). $_CLICK"
  ${LangFileString} MUI_INNERTEXT_LICENSE_BOTTOM_RADIOBUTTONS "Se aceitar os termos da licenza, seleccione a primeira opci?n abaixo. Cumpre aceitar o contrato para instalar o $(^NameDA). $_CLICK"
!endif

!ifdef MUI_UNLICENSEPAGE
  ${LangFileString} MUI_UNTEXT_LICENSE_TITLE "Contrato de licenza"
  ${LangFileString} MUI_UNTEXT_LICENSE_SUBTITLE "Por favor, verifique os termos da licenza antes de desinstalar o $(^NameDA)."
  ${LangFileString} MUI_UNINNERTEXT_LICENSE_BOTTOM "Se aceitar os termos da licenza, clique en 'Aceito' para continuar. Cumpre aceitar o contrato para desinstalar o $(^NameDA)."
  ${LangFileString} MUI_UNINNERTEXT_LICENSE_BOTTOM_CHECKBOX "Se aceitar os termos da licenza, clique na caixa de selecci?n abaixo. Cumpre aceitar o contrato para desinstalar o $(^NameDA). $_CLICK"
  ${LangFileString} MUI_UNINNERTEXT_LICENSE_BOTTOM_RADIOBUTTONS "Se aceitar os termos da licenza, seleccione a primeira opci?n abaixo. Cumpre aceitar o contrato para desinstalar o $(^NameDA). $_CLICK"
!endif

!ifdef MUI_LICENSEPAGE | MUI_UNLICENSEPAGE
  ${LangFileString} MUI_INNERTEXT_LICENSE_TOP "Prema Page Down para ver o restante da licenza."
!endif

!ifdef MUI_COMPONENTSPAGE
  ${LangFileString} MUI_TEXT_COMPONENTS_TITLE "Escolla de componentes"
  ${LangFileString} MUI_TEXT_COMPONENTS_SUBTITLE "Escolla que caracter?sticas do $(^NameDA) que desexa instalar."
  ${LangFileString} MUI_INNERTEXT_COMPONENTS_DESCRIPTION_TITLE "Descrici?n"
!endif

!ifdef MUI_UNCOMPONENTSPAGE
  ${LangFileString} MUI_UNTEXT_COMPONENTS_TITLE "Escoller componentes"
  ${LangFileString} MUI_UNTEXT_COMPONENTS_SUBTITLE "Escolla que caracter?sticas do $(^NameDA) desexa desinstalar."
!endif

!ifdef MUI_COMPONENTSPAGE | MUI_UNCOMPONENTSPAGE
  !ifndef NSIS_CONFIG_COMPONENTPAGE_ALTERNATIVE
    ${LangFileString} MUI_INNERTEXT_COMPONENTS_DESCRIPTION_INFO "Posicione o rato sobre un componente para ver a sua descrici?n."
  !else
    ${LangFileString} MUI_INNERTEXT_COMPONENTS_DESCRIPTION_INFO "Posicione o rato sobre un componente para ver a sua descrici?n."
  !endif
!endif

!ifdef MUI_DIRECTORYPAGE
  ${LangFileString} MUI_TEXT_DIRECTORY_TITLE "Escolla do local da instalaci?n"
  ${LangFileString} MUI_TEXT_DIRECTORY_SUBTITLE "Escolla a direct?ria na cal desexa instalar o $(^NameDA)."
!endif

!ifdef MUI_UNDIRECTORYSPAGE
  ${LangFileString} MUI_UNTEXT_DIRECTORY_TITLE "Escolla o Local de desinstalaci?n"
  ${LangFileString} MUI_UNTEXT_DIRECTORY_SUBTITLE "Escolla a direct?ria de onde pretende desinstalar o $(^NameDA)."
!endif

!ifdef MUI_INSTFILESPAGE
  ${LangFileString} MUI_TEXT_INSTALLING_TITLE "Instalando"
  ${LangFileString} MUI_TEXT_INSTALLING_SUBTITLE "Por favor, agarde entanto o $(^NameDA) est? sendo instalado."
  ${LangFileString} MUI_TEXT_FINISH_TITLE "Instalaci?n completa"
  ${LangFileString} MUI_TEXT_FINISH_SUBTITLE "A instalaci?n concluiu con suceso."
  ${LangFileString} MUI_TEXT_ABORT_TITLE "Instalaci?n Abortada"
  ${LangFileString} MUI_TEXT_ABORT_SUBTITLE "A instalaci?n concluiu sen suceso."
!endif

!ifdef MUI_UNINSTFILESPAGE
  ${LangFileString} MUI_UNTEXT_UNINSTALLING_TITLE "Desinstalando"
  ${LangFileString} MUI_UNTEXT_UNINSTALLING_SUBTITLE "Por favor, agarde entanto se desinstala o $(^NameDA)."
  ${LangFileString} MUI_UNTEXT_FINISH_TITLE "Desinstalaci?n completa"
  ${LangFileString} MUI_UNTEXT_FINISH_SUBTITLE "A desinstalaci?n concluiu con suceso."
  ${LangFileString} MUI_UNTEXT_ABORT_TITLE "Desinstalaci?n abortada"
  ${LangFileString} MUI_UNTEXT_ABORT_SUBTITLE "A desinstalaci?n non concluiu con suceso"
!endif

!ifdef MUI_FINISHPAGE
  ${LangFileString} MUI_TEXT_FINISH_INFO_TITLE "Concluindo o Asistente de instalaci?n do $(^NameDA)"
  ${LangFileString} MUI_TEXT_FINISH_INFO_TEXT "Instalou-se o $(^NameDA) no seu computador.$\r$\n$\r$\nClique en Rematar para fechar este asistente."
  ${LangFileString} MUI_TEXT_FINISH_INFO_REBOOT "Cumpre reiniciar o seu computador para conclu?r a instalaci?n do $(^NameDA). Desexa reiniciar agora?"
!endif

!ifdef MUI_UNFINISHPAGE
  ${LangFileString} MUI_UNTEXT_FINISH_INFO_TITLE "Conclu?ndo o asistente de desinstalaci?n do $(^NameDA)"
  ${LangFileString} MUI_UNTEXT_FINISH_INFO_TEXT "Eliminou-se $(^NameDA) do seu computador.$\r$\n$\r$\nClique em Rematar para fechar este asistente."
  ${LangFileString} MUI_UNTEXT_FINISH_INFO_REBOOT "Cumpre reiniciar o seu computador para conclu?r a desinstalaci?n do $(^NameDA). Desexa reinici?-lo agora?"
!endif

!ifdef MUI_FINISHPAGE | MUI_UNFINISHPAGE
  ${LangFileString} MUI_TEXT_FINISH_REBOOTNOW "Reiniciar agora"
  ${LangFileString} MUI_TEXT_FINISH_REBOOTLATER "Prefiro reinicia-lo manualmente despois"
  ${LangFileString} MUI_TEXT_FINISH_RUN "&Executar $(^NameDA)"
  ${LangFileString} MUI_TEXT_FINISH_SHOWREADME "&Mostrar Leame"
  ${LangFileString} MUI_BUTTONTEXT_FINISH "&Rematar"  
!endif

!ifdef MUI_STARTMENUPAGE
  ${LangFileString} MUI_TEXT_STARTMENU_TITLE "Escolla un cartafol do Menu Iniciar"
  ${LangFileString} MUI_TEXT_STARTMENU_SUBTITLE "Escolla un cartafol do Menu Iniciar para os atallos do programa."
  ${LangFileString} MUI_INNERTEXT_STARTMENU_TOP "Seleccione o cartafol do Menu Iniciar no que desexa criar os atallos do programa. Tam?n ? pos?bel dixitar un nome para criar un novo cartafol. "
  ${LangFileString} MUI_INNERTEXT_STARTMENU_CHECKBOX "Non criar atallos"
!endif

!ifdef MUI_UNCONFIRMPAGE
  ${LangFileString} MUI_UNTEXT_CONFIRM_TITLE "Desinstalar $(^NameDA)"
  ${LangFileString} MUI_UNTEXT_CONFIRM_SUBTITLE "Eliminar o $(^NameDA) do seu computador."
!endif

!ifdef MUI_ABORTWARNING
  ${LangFileString} MUI_TEXT_ABORTWARNING "Realmente desexa cancelar a instalaci?n do $(^Name)?"
!endif

!ifdef MUI_UNABORTWARNING
  ${LangFileString} MUI_UNTEXT_ABORTWARNING "Realmente desexa cancelar a desinstalaci?n do $(^Name)?"
!endif
