/* 
 * File:   unimgr_commands.h
 * Author: Michal Hucik <hucik@ordoz.com>
 *
 * Created on 22. června 2018, 9:55
 * 
 * 
 * ----------------------------- License -------------------------------------
 * 
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 * 
 * ---------------------------------------------------------------------------
 */


#ifndef UNIMGR_COMMANDS_H
#define UNIMGR_COMMANDS_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 *
 * Seznam prikazu repozitare
 *
 */


// Prace s repozitarem:
#define	cmdRESET	0x00	// Reset celeho repozitare. Nastaveni ASCII. 
                                // Sync a uzavreni otevrenych souboru. 
                                // Uzavreni otevrenych adresaru. CWD se nastavi na korenovy adresar.
                            
#define	cmdASCII	0x01	// I/O translator nastavime na ASCII.

#define	cmdSHASCII	0x02	// I/O translator nastavime na SharpASCII.

#define	cmdSTSR		0x03	// Ukazatel statusu se nastavi na zacatek. 
                                // Jinym zpusobem se na statusu neprojevi. Aktualni prikaz nebude prerusen.

#define	cmdSTORNO	0x04	// Okamzite ukonceni prave vkladaneho, 
                                // nebo vykonavaneho prikazu (takovy maly reset pro uvolneni data portu).

#define cmdREV		0x05	// Sdeli revizi firmware v txt tvaru.
                                //
                                // Vystup:	string

#define cmdREVD		0x06	// Sdeli revizi firmware v binarnim tvaru.
                                //
                                // Vystup:	DWORD (uc1) nebo 4B {major,minor,subtype,pc_type} (uc3)

/* Pouze v uc3 firmware - v uc1 neexistovaly. V emulátoru obsluhované
 * jako warning "ignored - not yet emulated" v uc3 simulaci, default-case
 * "Not implemented" v uc1 simulaci. */
#define cmdBOOT         0x07    // uc3: hw reset počítače
#define cmdGDGSYNC      0x0B    // uc3: GDG sync (jen MZ-800)

// Konfigurace a nastaveni unikarty:
#define	cmdFDDMOUNT	0x10	// Nastavi DSK image pro prislusnou mechaniku a ulozi konfiguraci.
                                // Pokud uz je v mechanice nejaky DSK, tak provede jeho sync a odpoji se.
                                //
                                // Vstup:	1B - cislo mechaniky
                                //		string - DSK image path/filename (pokud je parametr prazdny, tak se mechanika odmountuje a zustane prazdna)

#define cmdINTCALLER	0x11	// Sdeli kdo je volajicim interruptu. Kody volajicich periferii jsou definovany v mzint.h
                                //
                                // Vystup:	1B - mzintFLAG

// Prace s filesystemem:
#define	cmdGETFREE	0x20	// Sdeli celkovy pocet a pocet volnych sektoru na disku.
                                //
                                // Vystup:	DWORD - celkovy pocet sektoru
                                //		DWORD - pocet volnych sektoru
                                
#define	cmdCHDIR	0x21	// Zmena CWD.
                                //
                                // Vstup:	string - path, nebo dirname

#define	cmdGETCWD	0x22	// Sdeli aktualni CWD.
                                //
                                // Vystup:	string, 0x0d

// Prace se soubory a adresari:
#define	cmdSTAT		0x30	// Informace o souboru, nebo adresari.
                                //
                                // Vstup:	string
                                // Vystup:	binarni struktura FILINFO
                                
#define	cmdUNLINK	0x31	// Smazat soubor, nebo adresar.
                                //
                                // Vstup:	string
                                
#define	cmdCHMOD	0x32	// Zmena atributu souboru, nebo adresare. 
                                //
                                // Vstup:	1B - atributy
                                //		1B - maska 
                                //		string - filename/dirname
                                //
                                //
                                // 	0x01	AM_RDO	Read only
                                // 	0x02	AM_HID	Hidden
                                // 	0x04	AM_SYS	System
                                // 	0x20	AM_ARC	Archive
                                //
                                // Napr. nastavit AM_RDO a smazat AM_ARC (ostatni atributy zustanou nezmeneny):
                                //
                                //	attr = AM_RDO
                                //	mask = AM_RDO | AM_ARC
                                //     
                                    
#define	cmdUTIME	0x33	// Nastaveni casove znacky souboru.
                                //
                                // Vstup:	6B - D,M,Y-1980,H,M,S
                                //		string - filename/dirname

#define	cmdRENAME	0x34	// Prejmenovani souboru, nebo adresare.
                                //
                                // Vstup:	string - oldname
                                //		string - newname

// Prace s adresarem:
#define	cmdMKDIR	0x40	// Vytvori adresar. 
                                //
                                // Vstup:	string - dirname
                                
#define	cmdREADDIR	0x41	// Otevre adresar a pripravi na data port binarni vystup pro cteni.
                                // Pokud byl otevreny nejaky soubor, tak se provede sync() a zavre se.
                                //
                                // Vstup:	string - dirname
                                // Vystup:	FILINFO - binarni struktura pro kazdou polozku adresare
                                
#define	cmdFILELIST	0x42	// Otevre adresar a pripravi na data port textovy vystup pro cteni.
                                // Pokud byl otevreny nejaky soubor, tak se provede sync() a zavre se.
                                //
                                // Vstup:	string - dirname
                                // Vystup:	string filename[/],0x0d,string size,0x0d
                                
#define	cmdNEXT		0x43	// Provede okamzity presun na dalsi polozku adresare v prave 
                                // vykonavanem READDIR, nebo FILELIST.


// Prace se souborem:
#define	cmdOPEN		0x50	// Otevre soubor v pozadovanem rezimu.
                                //
                                // Vstup:	1B - rezim viz. FatFS 
                                //		string - filename
                                //
                                //	0x00	FA_OPEN_EXISTING
                                //	0x01	FA_READ
                                //	0x02	FA_WRITE
                                //	0x04	FA_CREATE_NEW
                                //	0x08	FA_CREATE_ALWAYS
                                //	0x10	FA_OPEN_ALWAYS
                                //	0x20	FA_UNIMGR_ASCII_CNV
                                //
                                
#define	cmdSEEK		0x51	// Zmena pozice v otevrenem souboru.
                                //
                                // Vstup:	1B - rezim 
                                //			0x00 - od zacatku
                                //			0x01 - od konce
                                //			0x02 - relativne nahoru
                                //			0x03 - relativne dolu
                                //		DWORD - pocet bajtu o ktere se ma ukazatel posunout

#define	cmdTRUNC	0x52	// Zkrati prave otevreny soubor na velikost odpovidajici soucasne pozici ukazatele.

#define	cmdSYNC		0x53	// Provede sync() prave otevreneho souboru.

#define	cmdCLOSE	0x54	// Provede sync() a zavre prave otevreny soubor.

#define	cmdTELL		0x55	// Sdeli pozici ukazatele v prave otevrenem souboru.
                                //
                                // Vystup:	DWORD
                                
#define	cmdSIZE		0x56	// Sdeli velikost prave otevreneho souboru.
                                //
                                // Vystup:	DWORD

// Prace s RTC:
#define	cmdRTCSETD	0x60	// Nastaveni datumu.
                                //
                                // Vstup:	3B - D,M,Y-1980

#define	cmdRTCSETT	0x61	// Nastaveni casu.
                                //
                                // Vstup:	3B - H,M,S
                                
#define	cmdRTCGETD	0x62	// Vrati aktualni datum.
                                //
                                // Vystup:	3B - D,M,Y-1980
                                
#define	cmdRTCGETT	0x63	// Vrati aktualni cas.
                                //
                                // Vystup:	3B - H,M,S

// USART:
#define cmdUSHELLON	0x70	// Zapne Ushell_USART, vypne emu_SIO.

#define cmdUSHELLOFF	0x71	// Vypne Ushell_USART, zapne emu_SIO.

#define	cmdUSARTBPS	0x72	// Nastaveni BPS pro USART1.
                                //
                                // Vstup:	DWORD - rychlost




// DNS Resolver:
//
// Pro DNS dotazy mame v multitasku k dispozici nekolik vlaken.
// Kazde vlakno se muze nezavisle dotazovat bud na domenove jmeno, nebo na IP adresu.
//
// Vlozeni dotazu probiha prikazem cmdRLSVQUERYA, nebo cmdRLSVQUERYN. Po jeho uspesnem vlozeni
// obdrzime cislo vlakna, ktere vyrizuje nas dotaz. Pokud dotaz nelze spustit, tak obdrzime
// hodnotu -1 (0xff) pokud DNS resolver nebezi, nebo -2 (0xfe) pokud neni k dispozici zadne volne vlakno.
// 
// U kazdeho jednotliveho dotazu se nastavuje jeho interrupt mode. Jestlize ma vlakno v interrupt mode 
// hodnotu 1, tak jakmile je tento dotaz zodpovezen, nebo ukoncen s chybou, tak Unikarta posle do Sharpa interrupt.
// Zaroven hodnota interrupt mode 1 zajisti uzamceni vlakna proti tomu, aby bylo prepsano jinym dotazem drive,
// nez bude precten vysledek.
//
// Precteni vysledku vlakna se provede prikazem cmdRLSVTHREAD. Tento prikaz deaktivuje interrupt
// vytvoreny timto vlaknem a odemkne vlakno pro dalsi pouziti.
//
// Vlakna u kterych neni nastaven interrupt mode mohou byt okamzite po dokonceni dotazu opet pouzita.
//
//
#define cmdRSLVSTRT	0x80	// Zapne, nebo prekonfiguruje DNS resolver.
                                //
                                // Vstup:	4B - IP adresa DNS serveru (a.b.c.d):
                                //		1. a
                                //		2. b
                                //		3. c
                                //		4. d
                                //
                                // Pokud jiz resolver bezi, tak se pouze zmeni adresa DNS serveru.
                                // Jestlize pri teto zmene prave probihaly nejake dotazy, tak samozrejme skonci jako STATE_ERROR. 
                                // 
                                // Pokud se resolver nepodarilo spustit, tak je na status master portu vystaven ERROR bit.
                                // TODO: mel by byt reportovan i nejaky INTERNAL ERROR CODE - prozatim je nemame implementovany a
                                // vracime vzdy 0x00.
                                
                                
#define cmdRSLVSTOP	0x81	// Vypne DNS resolver.


#define cmdRSLVGCFG	0x82	// Precteni konfigurace DNS resolveru.
                                //
                                // Vystup: 4B - ipadresa
                                // 
                                // Pokud nameserver nebezi (viz cmdRSLVSTATUS), vraci 00 00 00 00


#define cmdRSLVGDOMAIN	0x83	// Precteni implicitni domeny.
                                //
                                // Vystup: string, 0x0d

#define cmdRSLVSDOMAIN	0x84	// Nastaveni implicitni domeny.
                                //
                                // Vstup: string, 0x0d

#define cmdRSLVSTATUS	0x85	// Sdeli status DNS resolveru:
                                //
                                // Vystup: 
                                //
                                //	1B - pocet vlaken, 
                                //           nebo 0x00 pokud DNS resolver nebezi
                                //	
                                //	Pokud 1. bajt nebyl 0x00, tak nasleduje tabulka ve ktere je kazde vlakno zastoupeno jednim bajtem.
                                //	Jsou razeny postupne od vlakna s ID 0 az po posledni.
                                //
                                //	0. - 3. bit:
                                //		0 - STATE_UNUSED
                                //		1 - STATE_NEW
                                //		2 - STATE_ASKING
                                //		4 - STATE_DONE
                                //		8 - STATE_ERROR
                                //
                                //	7. bit - interrupt mode
                                //		Pokud je bit nastaven, tak to znamena, ze po vyrizeni vlakna (STATE_DONE, nebo STATE_ERROR)
                                //		bude poslan do Sharpa interrupt.


#define cmdRLSVQUERYA	0x86	// Zadost o preklad jmena na IP adresu (query):
                                //
                                // Vstup:
                                //	1B - interrupt mode:
                                //		0x00 - rezim interruptu je pro tento dotaz vypnuty
                                //		0x01 - jakmile prejde vlakno s timto dotazem do STATE_DONE, 
                                //			nebo STATE_ERROR, tak posle do Sharpa interrupt
                                //
                                //	string. 0x0d - domenove jmeno (pokud je v resolveru nakonfigurovana
                                //			implicitni domena, tak staci pouzit jen prvni cast domenoveho 
                                //		   	jmena a resolver si za nej sam prida tecku a nazev domeny)
                                //
                                // Vystup: 1B cislo vlakna, ktere vyrizuje tento dotaz, nebo:
                                //	0xff - resolver nebezi + status master port hlasi ERROR
                                // 	TODO: vystavit internal error
                                

#define cmdRLSVQUERYN	0x87	// Zadost o preklad IP adresy na jmeno (query):
                                //
                                // Vstup:
                                //	1B - mode:
                                //		1. bit je nastaveni interruptu
                                //			0 - interrupt neni aktivovan
                                //			1 - po dokonceni posleme do Sharpa interrupt
                                //	4B - IP adresa
                                //
                                // Vystup: 1B cislo vlakna, ktere vyrizuje tento dotaz, nebo:
                                //	0xff - resolver nebezi + status master port hlasi ERROR
                                //      TODO: vystavit internal error
                                                                
                                

#define cmdRLSVTHREAD	0x88	// Precte vlakno z resolveru.
                                //
                                // Vstup - 1B cislo vlakna                                
                                //
                                // Vystup: 
                                //	1B status (viz. cmdRSLVSTATUS), nebo:
                                //		0xff - pokud cislo vlakna neni validni
                                //		(v master status portu vystaven ERROR bit)
                                //		TODO: internal error
                                //
                                //	Nasledujici data budou validni jen v pripade, ze 1. bajt mel hodnotu STATE_DONE.
                                //
                                //	1B typ dotazu:
                                //		0x00 - podle jmena se resolvovala IP
                                //		0x01 - podle IP se hledalo jmeno
                                //
                                //	4B - IP adresa
                                //
                                //	1B - delka domenoveho jmena + 0x0d
                                //
                                //	string, 0x0d - domenove jmeno
                                //
                                // Pokud je status vlakna DONE, nebo ERROE a pokud byl pri dotazu u vlakna pozadovan interrupt, 
                                // tak se timto prikazem pro dane vlakno interrupt rezim deaktivuje a vlakno je otevreno pro 
                                // dalsi pouziti.
        

#define cmdRLSVLOOKA	0x89	// Lookup - prohledne jiz uspesne dokoncene thready, zda se se v nich nenachazi preklad domenoveho jmena na IP adresu
                                //
                                // Vstup:
                                //
                                //	string. 0x0d - domenove jmeno (pokud je v resolveru nakonfigurovana
                                //			implicitni domena, tak staci pouzit jen prvni cast domenoveho 
                                //		   	jmena a resolver si za nej sam prida tecku a nazev domeny)
                                //
                                // Vystup: 1B - odpoved, zda byl lookup uspesny, nebo:
                                //
                                //		0 - nenalezeno
                                //		1 - nalezeno
                                //
                                //	Pokud bylo nalezeno, tak nasleduji 4B s IP adresou.
                                



#define cmdRLSVLOOKN	0x8A	
                                // Lookup - prohledne jiz uspesne dokoncene thready, zda se se v nich nenachazi preklad IP adresy na jmeno.
                                //
                                // Vstup:
                                //
                                //	4B - IP adresa
                                //
                                // Vystup: 
                                //	1B - delka domenoveho jmena, nebo
                                //		0x00 - nenalezeno
                                //
                                //	string, 0x0d
                                

#define cmdRLSVIP2TXT	0x8b	// Prevod IP adresy na text - "192.168.0.1\x0d"
                                //
                                // Vstup: 4B - IP adresa
                                //
                                //
                                // Vystup: 
                                //
                                //	1B delka retezce
                                //	string, 0x0d
                                //


#define cmdRLSVTXT2IP	0x8c	// Prevod textu - "192.168.0.1\x0d" na IP.
                                //
                                // Vstup: string + 0x0d
                                //
                                //
                                // Vystup: 
                                //
                                //	1B - uspesnost prevodu
                                //		0 - nepovedlo se
                                //		1 - povedlo se a nasleduje IP
                                //
                                //	4B - IP adresa



//#define cmdWEBC

/* uc3 networking - obsluhovane warningem v emulátoru (TCP/IP a DNS
 * vyžadují ESP8266/WiFi modul z unicard3 HW, není emulováno). */
#define cmdTCPSTATUS    0x80    // uc3: get TCP status
#define cmdTCPIP        0x81    // uc3: set IP
#define cmdTCPPORT      0x82    // uc3: set port
#define cmdTCPOPEN      0x83    // uc3: open TCP connection
#define cmdTCPCLOSE     0x84    // uc3: close TCP connection
#define cmdTCPSEND      0x85    // uc3: send
#define cmdTCPRECV      0x86    // uc3: receive
#define cmdDNSREQ       0x88    // uc3: DNS request
#define cmdDNSRES       0x89    // uc3: DNS response


// Interni prikazy - provadi se jinak, nez pres CMD port:

#define cmdINTGETC		0xf0	// Getchar - precteni 1B na data portu z otevreneho souboru.

#define cmdINTPUTC		0xf1	// Putchar - zapis 1B na data port do otevreneho souboru.




#ifdef __cplusplus
}
#endif

#endif /* UNIMGR_COMMANDS_H */

