
ACHKIK Chaïmae   3804488,
CIOCAN Clara     3801875,
ENGELKE Timon    21137135

README
======


Étape 1 : écriture
------------------

## Chaînage des blocs d'index

	Une version d'un fichier ouichefs est représentée par une structure ouichefs_file_index_block contenant un tableau des numéros de blocs composant la version. On ajoute à cette structure un champ permettant de chaîner les versions entre elles :

    - uint32_t previous_block_number : le numéro du bloc index de la version précedente.

	Le choix de numéros de blocs comme méthode de chaînage et a été effectué afin d'éviter l'utilisation de pointeurs, qui deviennent invalides après chaque redémarrage du système, impliquée par l'utilisation de la structure list_head. La sauvegarde des numéros de blocs d'index nous permet de tracer les numéros de blocs index des versions précedentes directement en mémoire physique.

	Il est important de mentionner qu'un changement de nombre de blocs s'est avéré nécessaire afin de respecter la consigne (les modifications apportées doivent toujours tenir sur un seul bloc).

```C
struct ouichefs_file_index_block {
	uint32_t previous_block_number;
	uint32_t blocks[(OUICHEFS_BLOCK_SIZE >> 2) - 1];
};
```

## Modification des fonctions d'écriture

    Afin de permettre la sauvegarde des versions précedentes du fichier avant chaque modification d'écriture, on a décidé de modifier directement la fonction ouichefs_write_begin comme suit :

```C
static int ouichefs_write_begin(struct file *file,
				struct address_space *mapping, loff_t pos,
				unsigned int len, unsigned int flags,
				struct page **pagep, void **fsdata)
{
	struct ouichefs_sb_info *sbi = OUICHEFS_SB(file->f_inode->i_sb);
	int err;
	struct inode *inode = file->f_inode;
	uint32_t nr_allocs = 0;
	struct buffer_head *bh_index, *bh_new_index;
	struct buffer_head *bh_new_data_block, *bh_data_block;
	struct ouichefs_inode_info *ci = OUICHEFS_INODE(inode);
	struct super_block *sb = inode->i_sb;
	struct ouichefs_file_index_block *index, *new_index;
	uint32_t new_index_no, new_block_no;
	int i;

    ... // Pas de changement

	if (ci->index_block != ci->last_index_block) {
		pr_err("Unable to write to old version!\n");
		return -EINVAL;
	}

	if ((long long) file->private_data == -1) {
		file->private_data = (void *) pos;
		/* Duplicate index block and data */
		bh_index = sb_bread(sb, ci->index_block);
		if (!bh_index)
			return -EIO;
		index = (struct ouichefs_file_index_block *)bh_index->b_data;

		new_index_no = get_free_block(sbi);
		if (!new_index_no)
			return -ENOSPC;

		bh_new_index = sb_bread(sb, new_index_no);
		new_index = (struct ouichefs_file_index_block *)bh_new_index->b_data;
		memset(new_index, 0, sizeof(*new_index));

		for (i = 0; i < sizeof(index->blocks) / sizeof(uint32_t); i++) {
			if (index->blocks[i] == 0)
				continue;

			new_block_no = get_free_block(sbi);
			if (!new_block_no)
				return -ENOSPC;
			pr_info("Duplicating block %x to %x\n", index->blocks[i], new_block_no);

			bh_new_data_block = sb_bread(sb, new_block_no);
			bh_data_block = sb_bread(sb, index->blocks[i]);
			memcpy(bh_new_data_block->b_data, bh_data_block->b_data,
					OUICHEFS_BLOCK_SIZE);
			mark_buffer_dirty(bh_new_data_block);
			sync_dirty_buffer(bh_new_data_block);
			brelse(bh_new_data_block);
			brelse(bh_data_block);
			new_index->blocks[i] = new_block_no;
		}

		new_index->previous_block_number = ci->index_block;
		ci->index_block = new_index_no;
		ci->last_index_block = new_index_no;
		mark_buffer_dirty(bh_new_index);
		mark_buffer_dirty(bh_index);
		sync_dirty_buffer(bh_new_index);
		sync_dirty_buffer(bh_index);
		brelse(bh_index);
		brelse(bh_new_index);
	}
}
```
    Explication :
        Les tests effectués concernaient dans un premier temps des écritures d'une taille inférieure à la taille d'un bloc. Les modifications apportées se sont montrés insuffisantes lors du traitement d'écritures de données de tailles supérieures à 1 bloc.
        Afin de palier ce problème, on a utilisé le champ private_data du fichier file pour stocker les deux informations suivantes : est-ce la première fois qu'on appele la fonction pour l'écriture courante (chacune des fonctions write_begin et write_end est appelée une fois par bloc écrit), et la position du début de modification.
        Ce champ, lors de sa lecture, va nous permettre de savoir si c'est nécessaire de dupliquer les données (premier appel à la fonction, le champ private_data est donc toujours initialisé à -1), ainsi que le bloc à partir duquel on commence à dédupliquer les données.
        En effet, à chaque écriture, le paramètre pos permet de savoir la position dans le fichier à partir de laquelle on commence la modification/écriture. Les blocs précedents, non concernés par cette écriture, feront objet à une déduplication vu que leurs données restent inchangées.
        On est conscient qu'il peut s'agir d'une reécriture des mêmes données, mais cet événement n'est pas assez occurent pour changer l'implémetation de cette fonctionnalité.

    Le reste des fonctions d'écriture n'ont pas subis de changements notables.


Étape 2 : utilitaire de déboguage
---------------------------------

## Fichier du debugfs fonctionnel

    Les fonctionnalités de l'utilitatire de déboguage ont été implémentées dans le fichier fs.c comme suit :

+ Fichiers à include :
```C
...
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include <linux/buffer_head.h>
...
```

+ On recupère le superblock dans le champ private de la structure file dans le but de pouvoir l'utiliser dans la fonction debug_seq_show. Ce champ est rempli à l'aide de la fonction single_open, appelée par debugfs_open, qui affecte le contenu du dernier paramètre passé au champ private du seq_file ouvert.
La fonction debugfs_open prend comme paramètre, en addition au fichier souhaitant son ouverture, une structure inode contenant dans son champ i_private la structure super_block de la partiton concernée. Cette dernière est ajoutée à la structure inode dans la fonction ouichefs_debug_file qui crée le fichier debugfs.

+ Ajout du champ : struct dentry *debug_file à la fin de la structure ouichefs_sb_info. Ce champ va nous permettre de récupérer facilement les informations concernant le fichier debugfs ainsi que sa destruction.
```C
// Fonction d'affichage de données
static int debug_seq_show(struct seq_file *file, void *v)
{
	int ino;
	struct super_block *sb = (struct super_block *)file->private;
	struct ouichefs_sb_info *sbi = OUICHEFS_SB(sb);
	struct inode *inode;
	struct ouichefs_inode_info *ci;
	uint32_t first_block_no, cur_block_no;
	struct buffer_head *bh;
	int count;
	struct ouichefs_file_index_block *index;
	char c[100];

	for (ino = 0; ino < sbi->nr_inodes; ino++) {
		inode = ouichefs_iget(sb, ino);
		ci = OUICHEFS_INODE(inode);

		if (inode->i_nlink == 0)
			goto iput;

		if (!S_ISREG(inode->i_mode))
			goto iput;

		count = 0;

		memset(c, 0, sizeof(c));

		first_block_no = ci->last_index_block;
		cur_block_no = first_block_no;

		while (cur_block_no != 0) {
			count++;
			snprintf(c, 100, "%s, %d", c, cur_block_no);

			bh = sb_bread(sb, cur_block_no);
			if (!bh)
				continue;

			index = (struct ouichefs_file_index_block *)bh->b_data;
			cur_block_no = index->previous_block_number;


		}
		seq_printf(file, "%d %d [%s]\n", ino, count, c + 2);
iput:
		iput(inode);
	}
	return 0;
}
```


```C
// Fonction open. Utilisation de single_open.
static int debugfs_open(struct inode *inode, struct file *file)
{
	return single_open(file, debug_seq_show, inode->i_private);
}

static const struct file_operations debug_fops = {
	.owner = THIS_MODULE,
	.open = debugfs_open,
	.read = seq_read,
	.release = single_release,
};
```
```C
int ouichefs_debug_file(struct super_block *sb)
{
	struct dentry *debugfs_file;
	struct ouichefs_sb_info *sbi = OUICHEFS_SB(sb);

	debugfs_file = debugfs_create_file(sb->s_id, 0400, NULL, NULL,
					&debug_fops);
	debugfs_file->d_inode->i_private = sb;
	if (!debugfs_file) {
		pr_err("Debugfs file creation failed\n");
		return -EIO;
	}
	sbi->debug_file = debugfs_file;
	return 0;
}
```

+ Ajout de la création du fichier lors du 'mount' (monter le système fichiers) :

```C
struct dentry *ouichefs_mount(struct file_system_type *fs_type, int flags,
			      const char *dev_name, void *data)
{
    ...
    ouichefs_debug_file(dentry->d_inode->i_sb); // Creation du fichier debugfs
    return dentry;
}
```
+ Destruction du fichier debugfs quand on démonte (unmount) le système fichiers :
```C
/*
 * Unmount a ouiche_fs partition
 */
void ouichefs_kill_sb(struct super_block *sb)
{
    struct ouichefs_sb_info *sbi = OUICHEFS_SB(sb);
    debugfs_remove(sbi->debug_file); // Destruction du fichier debugfs

    kill_block_super(sb);
    ...
}
```

Étape 3 : vue courante
----------------------

## Modification de la structure ouichefs_inode

    Le champ uint32_t last_index_block a été ajouté à la fin de la structure ouichefs_inode pour indiquer la dernière version du fichier.
    Ce même champ est également ajouté à la structure ouichefs_inode_info afin de faciliter l'accès à ce champ.

## Requête ioctl changement vue courante

    Implémentation de la requête ioctl pour le changement de la vue courante :

+ Dans le fichier ouichefs.h, ajout du define suivant pour la definition de la requête :
```C
         #define OUICHEFS_SHOW_VERSION	_IOR('O', 0, unsigned long)

```

+ Implémentation de la fonction ouichefs_change_file_version dans le fichier file.c qui prend en paramètre le fichier dont on veut changer de version et un entier indiquant la version souhaitée, et qui retourne un entier indiquant si le changement de version a bien réussit.
```C
        int ouichefs_change_file_version(struct file *file, int version)
        {
            struct super_block *sb = file->f_inode->i_sb;
            struct ouichefs_file_index_block *index_block;
            struct buffer_head *bh;
            struct ouichefs_inode_info *info = OUICHEFS_INODE(file->f_inode);
            uint32_t current_version_block = info->last_index_block;

            while (version > 0) {
                bh = sb_bread(sb, current_version_block);
                index_block = (struct ouichefs_file_index_block *)bh->b_data;
                current_version_block = index_block->previous_block_number;
                version--;
                if (current_version_block == 0)
                    return -EINVAL;
            }
            info->index_block = current_version_block;

            mark_inode_dirty(file->f_inode);
            invalidate_mapping_pages(file->f_inode->i_mapping, 0, -1); // Invalidation de la cache
            return 0;
        }
```

        Pour cette partie, une invalidation de cache s'est avérée nécessaire afin d'obliger la mise à jour des données à partir du disque et éviter la lecture de versions obsolètes à partir de la cache après un passage à une version antérieure.

+ Implémentation de la fonction ioctl :
 ```C
        long ouichefs_ioctl(struct file *file, unsigned int cmd, unsigned long argp)
        {
            switch (cmd) {
            case (OUICHEFS_SHOW_VERSION):
                return ouichefs_change_file_version(file, argp);
            default:
                return -EINVAL;
            }
            return 0;
        }
```


## Modification des fonctions d'écriture

    La principale modification apportée à la fonction d'écriture est l'ajout d'une vérification au début de la fonction afin de s'assurer que la version courante est la dernière version avant chaque écriture, c'est-à-dire que index_block vaut bien last_index_block. Si ce n'est pas le cas, on retourne une erreur et on effectue pas l'écriture.

```C
static int ouichefs_write_begin(struct file *file,
				struct address_space *mapping, loff_t pos,
				unsigned int len, unsigned int flags,
				struct page **pagep, void **fsdata)
{
    ... // Pas de changements
    if (ci->index_block != ci->last_index_block) {
        pr_err("Unable to write to old version!\n");
        return -EINVAL;
    }
    ... // Pas de changements
}
```

Étape 4 : restauration
----------------------

## Requête ioctl restauration

+ Definition de la requête  :

```C
 #define OUICHEFS_RESTORE_VERSION	_IO('O', 1)
 ```

+ Définition de la fonction ouichefs_restore_file_version qui permet la restauration d'une version précedente comme version actuelle et libère les blocs inutilisés.

```C
int ouichefs_restore_file_version(struct file *file)
{
	struct ouichefs_inode_info *info = OUICHEFS_INODE(file->f_inode);
	struct buffer_head *bh_index;
	struct super_block *sb = file->f_inode->i_sb;
	struct ouichefs_sb_info *sbi = OUICHEFS_SB(sb);
	struct ouichefs_file_index_block *index;
	uint32_t current_version_block = info->last_index_block;
	int i;

	while (current_version_block != info->index_block) {
		bh_index = sb_bread(sb, current_version_block);
		if (!bh_index)
			return -EIO;
		index = (struct ouichefs_file_index_block *)bh_index->b_data;

		for (i = 0; i < sizeof(index->blocks) / sizeof(uint32_t); i++) {
			if (index->blocks[i] == 0)
				continue;
			put_block(sbi, index->blocks[i]);
		}
		put_block(sbi, current_version_block);
		current_version_block = index->previous_block_number;
		brelse(bh_index);
	}
	info->last_index_block = info->index_block;
	return 0;
}
```
## Modification fonction ioctl

```C
long ouichefs_ioctl(struct file *file, unsigned int cmd, unsigned long argp)
{
	switch (cmd) {
	case (OUICHEFS_SHOW_VERSION):
		return ouichefs_change_file_version(file, argp);
	case (OUICHEFS_RESTORE_VERSION): // Nouvelle requête
		return ouichefs_restore_file_version(file);
	default:
		return -EINVAL;
	}
	return 0;
}
```
## Blocs libérés utilisables

    On rend les blocs libérés utilisables dans la fonction ouichefs_restore_file_version en effectuant un 'put_block' de chaque bloc libéré du tableau de blocs afin de signaler dans le bitmap du superbloc qu'il est libre, et donc utilisable, avant d'effectuer un 'put_block' du bloc index de la version libérée.


Étape 5 : déduplication
-----------------------

## Modification des fonctions d'écriture

        Le code suivant a été ajouté dans la fonction write_end afin d'implémenter le principe de déduplication :

```C
int64_t write_pos = (int64_t) file->private_data;
if (write_pos < pos) {
        index_block = ci->last_index_block;

        bh_index = sb_bread(sb, index_block);
        if (!bh_index)
                return -EIO;

        index = (struct ouichefs_file_index_block *)bh_index->b_data;
        prev_index_block = index->previous_block_number;

        bh_prev_index = sb_bread(sb, prev_index_block);
        if (!bh_prev_index)
                return -EIO;
        prev_index = (struct ouichefs_file_index_block *)bh_prev_index->b_data;
        for (i = 0; (i + 1) * OUICHEFS_BLOCK_SIZE < write_pos; i++) {
                cur_no = index->blocks[i];
                prev_no = prev_index->blocks[i];
                if ((cur_no == 0) || (prev_no == 0))
                        continue;
                if (cur_no == prev_no)
                        continue;

                pr_info("Delete block %x\n", cur_no);
                index->blocks[i] = prev_no;
                put_block(sbi, cur_no);
                mark_buffer_dirty(bh_index);
                sync_dirty_buffer(bh_index);
        }
        brelse(bh_prev_index);
        brelse(bh_index);
}
```

	Explication :
        write_pos ici est utilisé pour récupérer le champ file->private_data expliqué précedemment.
        On effectue dans la fonction write_end un test sur cette variable pour vérifier s'il s'agit de blocs déduplicables c'est-à-dire que les blocs figurent avant le bloc concerné par l'écriture. Si c'est le cas, on procède à la déduplication en mettant à jour les numéros de blocs dans la liste d'index pour les blocs concernés avant de les libérer.

## Libération synchrone

Afin d'implémenter cette fonctionnalité, deux cas peuvent être distingués :

- Pour la suppression de version : (requête restore de l'ioctl)
        Les principaux changements effectués concerne la fonction ouichefs_restore_file_version. Cette dernière a été modifié comme suit:

```C
	while (current_version_block != info->index_block) {
		bh_index = sb_bread(sb, current_version_block);
		if (!bh_index)
			return -EIO;
		index = (struct ouichefs_file_index_block *)bh_index->b_data;

		prev_version_block = index->previous_block_number;

		if (prev_version_block != 0) {
			bh_prev_index = sb_bread(sb, prev_version_block);
			if (!bh_prev_index)
				return -EIO;
			prev_index = (struct ouichefs_file_index_block *)bh_prev_index->b_data;
		}

		for (i = 0; i < sizeof(index->blocks) / sizeof(uint32_t); i++) {
			if (index->blocks[i] == 0)
				continue;
			if (prev_version_block != 0 && index->blocks[i] == prev_index->blocks[i])
				continue;
			put_block(sbi, index->blocks[i]);
		}
		put_block(sbi, current_version_block);
		current_version_block = index->previous_block_number;
		brelse(bh_index);

		if (prev_version_block != 0)
			brelse(bh_prev_index);
	}
	info->last_index_block = info->index_block;
```
        Explication :
        Le principal changement implémenté est une comparaison pour chaque version supprimée entre les numéros de blocs de sa liste d'index et ceux de la liste de la version précedente. Si deux numéros sont égaux, la libération du bloc correspondant n'est pas effectuée vu que celui-ci est toujours en cours d'utilisation par au moins une autre version.

- Pour la suppression du fichier (unlink, commande rm)
        Les changements de cette partie concernent la fonction ouichefs_unlink :

```C
while (bno != 0) {
        bh = sb_bread(sb, bno);
        if (!bh)
                goto clean_inode;
        file_block = (struct ouichefs_file_index_block *)bh->b_data;
        if (S_ISDIR(inode->i_mode))
                goto scrub;
        for (i = 0; i < sizeof(file_block->blocks) / sizeof(uint32_t); i++) {
                char *block;

                if (!file_block->blocks[i])
                        continue;

                bh2 = sb_bread(sb, file_block->blocks[i]);
                if (!bh2)
                        continue;
                block = (char *)bh2->b_data;
                memset(block, 0, OUICHEFS_BLOCK_SIZE);
                mark_buffer_dirty(bh2);
                put_block(sbi, file_block->blocks[i]);
                brelse(bh2);
        }
        /* Free index block */
        put_block(sbi, bno);
        bno = file_block->previous_block_number;
}
```
        Explication :
        La seule chose ajoutée à la fonction de base est une boucle qui parcourt l'ensemble des listes de blocs des différentes versions du fichier afin de libérer tous les blocs alloués.

## Libération asynchrone

        pr_err("Not implemented yet.\n");
